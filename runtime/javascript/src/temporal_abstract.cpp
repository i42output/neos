/*
  temporal_abstract.cpp

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

// Temporal: the abstract operations (exact integer arithmetic, options, rounding, ISO 8601 dates and times, durations,
// the ISO 8601 calendar and the arithmetic of relative durations), written from the Temporal specification text.

#include <algorithm>
#include <cmath>
#include <cstring>
#include "temporal.hpp"

namespace neos::javascript_runtime::temporal
{
    // --- 128-bit integers

    namespace
    {
        struct u128
        {
            std::uint64_t hi = 0u;
            std::uint64_t lo = 0u;
        };

        bool ge(u128 const& a, u128 const& b)
        {
            return a.hi != b.hi ? a.hi > b.hi : a.lo >= b.lo;
        }

        u128 sub(u128 const& a, u128 const& b)
        {
            u128 r;
            r.lo = a.lo - b.lo;
            r.hi = a.hi - b.hi - (a.lo < b.lo ? 1u : 0u);
            return r;
        }

        u128 shl1(u128 const& a, unsigned aBit)
        {
            u128 r;
            r.hi = (a.hi << 1) | (a.lo >> 63);
            r.lo = (a.lo << 1) | aBit;
            return r;
        }

        int bit_length(u128 const& a)
        {
            if (a.hi != 0u)
                return 128 - std::countl_zero(a.hi);
            if (a.lo != 0u)
                return 64 - std::countl_zero(a.lo);
            return 0;
        }

        unsigned bit(u128 const& a, int aIndex)
        {
            return aIndex >= 64 ? static_cast<unsigned>((a.hi >> (aIndex - 64)) & 1u) : static_cast<unsigned>((a.lo >> aIndex) & 1u);
        }

        void udivmod(u128 const& aNumerator, u128 const& aDenominator, u128& aQuotient, u128& aRemainder)
        {
            if (aNumerator.hi == 0u && aDenominator.hi == 0u)
            {
                aQuotient = { 0u, aNumerator.lo / aDenominator.lo };
                aRemainder = { 0u, aNumerator.lo % aDenominator.lo };
                return;
            }
            u128 q;
            u128 r;
            for (int i = bit_length(aNumerator) - 1; i >= 0; --i)
            {
                r = shl1(r, bit(aNumerator, i));
                if (ge(r, aDenominator))
                {
                    r = sub(r, aDenominator);
                    if (i >= 64)
                        q.hi |= 1ull << (i - 64);
                    else
                        q.lo |= 1ull << i;
                }
            }
            aQuotient = q;
            aRemainder = r;
        }

        u128 magnitude(i128 const& a)
        {
            auto const m = a.abs();
            return { m.hi(), m.lo() };
        }

        i128 from_u128(u128 const& a, bool aNegative)
        {
            auto const r = i128::from_parts(a.hi, a.lo);
            return aNegative ? -r : r;
        }

        void umul64(std::uint64_t a, std::uint64_t b, std::uint64_t& aHi, std::uint64_t& aLo)
        {
            auto const a0 = a & 0xFFFFFFFFull;
            auto const a1 = a >> 32;
            auto const b0 = b & 0xFFFFFFFFull;
            auto const b1 = b >> 32;
            auto const p00 = a0 * b0;
            auto const p01 = a0 * b1;
            auto const p10 = a1 * b0;
            auto const p11 = a1 * b1;
            auto const mid = (p00 >> 32) + (p01 & 0xFFFFFFFFull) + (p10 & 0xFFFFFFFFull);
            aLo = (p00 & 0xFFFFFFFFull) | (mid << 32);
            aHi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
        }
    }

    i128 operator*(i128 const& a, i128 const& b)
    {
        std::uint64_t hi = 0u;
        std::uint64_t lo = 0u;
        umul64(a.lo(), b.lo(), hi, lo);
        hi += a.lo() * b.hi() + a.hi() * b.lo();
        return i128::from_parts(hi, lo);
    }

    void divmod(i128 const& aNumerator, i128 const& aDenominator, i128& aQuotient, i128& aRemainder)
    {
        u128 q;
        u128 r;
        udivmod(magnitude(aNumerator), magnitude(aDenominator), q, r);
        aQuotient = from_u128(q, aNumerator.negative() != aDenominator.negative());
        aRemainder = from_u128(r, aNumerator.negative());
    }

    i128 operator/(i128 const& a, i128 const& b)
    {
        i128 q;
        i128 r;
        divmod(a, b, q, r);
        return q;
    }

    i128 operator%(i128 const& a, i128 const& b)
    {
        i128 q;
        i128 r;
        divmod(a, b, q, r);
        return r;
    }

    i128 floor_div(i128 const& aNumerator, i128 const& aDenominator)
    {
        i128 q;
        i128 r;
        divmod(aNumerator, aDenominator, q, r);
        if (!r.zero() && r.negative() != aDenominator.negative())
            q -= 1;
        return q;
    }

    i128 floor_mod(i128 const& aNumerator, i128 const& aDenominator)
    {
        return aNumerator - floor_div(aNumerator, aDenominator) * aDenominator;
    }

    int compare(i128 const& a, i128 const& b)
    {
        return a < b ? -1 : (b < a ? 1 : 0);
    }

    double ratio_to_double(i128 aNumerator, i128 aDenominator)
    {
        if (aDenominator.negative())
        {
            aNumerator = -aNumerator;
            aDenominator = -aDenominator;
        }
        bool const negative = aNumerator.negative();
        u128 q;
        u128 r;
        auto const d = magnitude(aDenominator);
        udivmod(magnitude(aNumerator), d, q, r);
        if (bit_length(q) == 0 && bit_length(r) == 0)
            return 0.0;
        // (a 64-bit mantissa (its top bit set), its exponent and whether anything below it is non-zero)
        std::uint64_t mantissa = 0u;
        int exponent = 0;
        bool sticky = false;
        auto const n = bit_length(q);
        if (n > 64)
        {
            auto const shift = n - 64;
            mantissa = shift >= 64 ? (q.hi >> (shift - 64)) : ((q.hi << (64 - shift)) | (q.lo >> shift));
            exponent = shift;
            u128 lowMask;
            if (shift >= 64)
                lowMask = { (1ull << (shift - 64)) - 1u, ~0ull };
            else
                lowMask = { 0u, (1ull << shift) - 1u };
            sticky = (q.hi & lowMask.hi) != 0u || (q.lo & lowMask.lo) != 0u || bit_length(r) != 0;
        }
        else
        {
            mantissa = q.lo;
            exponent = 0;
            while (mantissa < (1ull << 63))
            {
                r = shl1(r, 0u);
                unsigned b = 0u;
                if (ge(r, d))
                {
                    r = sub(r, d);
                    b = 1u;
                }
                mantissa = (mantissa << 1) | b;
                --exponent;
            }
            sticky = bit_length(r) != 0;
        }
        // (round to 53 bits, ties to even)
        auto top = mantissa >> 11;
        auto const rest = mantissa & 0x7FFull;
        bool roundUp = false;
        if (rest > 0x400u || (rest == 0x400u && (sticky || (top & 1u) != 0u)))
            roundUp = true;
        else if (rest == 0x400u && !sticky)
            roundUp = (top & 1u) != 0u;
        if (roundUp)
            ++top;
        auto const result = std::ldexp(static_cast<double>(top), exponent + 11);
        return negative ? -result : result;
    }

    double i128::to_double() const
    {
        return ratio_to_double(*this, i128{ 1 });
    }

    i128 i128::from_double(double aValue)
    {
        if (std::fabs(aValue) < 9.0e18)
            return i128{ static_cast<std::int64_t>(aValue) };
        int exponent = 0;
        auto const fraction = std::frexp(std::fabs(aValue), &exponent);
        auto const mantissa = static_cast<std::uint64_t>(std::ldexp(fraction, 53));
        auto const shift = exponent - 53;
        i128 r = i128::from_parts(0u, mantissa);
        for (int i = 0; i < shift; ++i)
            r = r + r;
        return aValue < 0.0 ? -r : r;
    }

    std::string i128::to_string() const
    {
        auto m = magnitude(*this);
        if (bit_length(m) == 0)
            return "0";
        std::string digits;
        u128 const ten{ 0u, 10u };
        while (bit_length(m) != 0)
        {
            u128 q;
            u128 r;
            udivmod(m, ten, q, r);
            digits.push_back(static_cast<char>('0' + r.lo));
            m = q;
        }
        if (negative())
            digits.push_back('-');
        std::reverse(digits.begin(), digits.end());
        return digits;
    }

    i128 const& ns_max_instant()
    {
        static i128 const sValue = i128{ 100000000 } * i128{ nsPerDay };
        return sValue;
    }

    i128 const& max_time_duration()
    {
        static i128 const sValue = i128{ 9007199254740992LL } * i128{ 1000000000 } - i128{ 1 };
        return sValue;
    }

    value make_bigint_from(i128 const& aValue)
    {
        auto& b = the_heap().make<bigint>();
        auto const m = magnitude(aValue);
        std::uint32_t const parts[4] = { static_cast<std::uint32_t>(m.lo), static_cast<std::uint32_t>(m.lo >> 32),
            static_cast<std::uint32_t>(m.hi), static_cast<std::uint32_t>(m.hi >> 32) };
        int n = 4;
        while (n > 0 && parts[n - 1] == 0u)
            --n;
        b.magnitude.assign(parts, parts + n);
        b.negative = n > 0 && aValue.negative();
        return value_of(b);
    }

    std::optional<i128> bigint_to_i128(value aBigInt)
    {
        auto const& b = cell_of<bigint>(aBigInt);
        if (b.magnitude.size() > 4u || (b.magnitude.size() == 4u && (b.magnitude[3] & 0x80000000u) != 0u))
            return {};
        std::uint32_t parts[4] = {};
        for (std::size_t i = 0u; i < b.magnitude.size(); ++i)
            parts[i] = b.magnitude[i];
        auto const r = i128::from_parts((static_cast<std::uint64_t>(parts[3]) << 32) | parts[2], (static_cast<std::uint64_t>(parts[1]) << 32) | parts[0]);
        return b.negative ? -r : r;
    }

    // --- errors and strings

    std::u16string u16(std::string const& aText)
    {
        return std::u16string(aText.begin(), aText.end());
    }

    void range_error(char const* aMessage)
    {
        throw_range_error(aMessage);
    }

    void type_error(char const* aMessage)
    {
        throw_type_error(aMessage);
    }

    // --- units, options and rounding

    namespace
    {
        char const* const sUnitSingular[] = { "year", "month", "week", "day", "hour", "minute", "second", "millisecond", "microsecond", "nanosecond" };
        char const* const sUnitPlural[] = { "years", "months", "weeks", "days", "hours", "minutes", "seconds", "milliseconds", "microseconds", "nanoseconds" };

        bool equals_ascii(std::u16string_view aText, char const* aAscii)
        {
            auto const n = std::strlen(aAscii);
            if (aText.size() != n)
                return false;
            for (std::size_t i = 0u; i < n; ++i)
                if (aText[i] != static_cast<char16_t>(aAscii[i]))
                    return false;
            return true;
        }

        // (GetOption with a list of strings: the index of the value in the list, or aDefault if the property is undefined)
        int get_string_option(value aOptions, char const* aProperty, std::initializer_list<char const*> aValues, int aDefault)
        {
            auto const v = get(aOptions, ascii(aProperty));
            if (is_undefined(v))
            {
                if (aDefault < 0)
                    throw_range_error(std::string{ aProperty } + " is required");
                return aDefault;
            }
            auto const& text = str(to_string(v));
            int index = 0;
            for (auto const* candidate : aValues)
            {
                if (equals_ascii(text, candidate))
                    return index;
                ++index;
            }
            throw_range_error(utf8(make_string(text)) + " is not a valid value for " + aProperty);
        }
    }

    i128 unit_length(unit aUnit)
    {
        switch (aUnit)
        {
        case unit::Day: return i128{ nsPerDay };
        case unit::Hour: return i128{ 3600000000000LL };
        case unit::Minute: return i128{ 60000000000LL };
        case unit::Second: return i128{ 1000000000LL };
        case unit::Millisecond: return i128{ 1000000LL };
        case unit::Microsecond: return i128{ 1000LL };
        default: return i128{ 1 };
        }
    }

    std::int64_t maximum_rounding_increment(unit aUnit)
    {
        switch (aUnit)
        {
        case unit::Hour: return 24;
        case unit::Minute: return 60;
        case unit::Second: return 60;
        case unit::Millisecond: return 1000;
        case unit::Microsecond: return 1000;
        case unit::Nanosecond: return 1000;
        default: return 0;
        }
    }

    value get_options_object(value aOptions)
    {
        if (is_undefined(aOptions))
            return make_object(Null);
        if (is_object(aOptions))
            return aOptions;
        throw_type_error("options must be an object or undefined");
    }

    value get_option_string(value aOptions, char const* aProperty)
    {
        auto const v = get(aOptions, ascii(aProperty));
        if (is_undefined(v))
            return Undefined;
        return to_string(v);
    }

    overflow get_overflow_option(value aOptions)
    {
        return get_string_option(aOptions, "overflow", { "constrain", "reject" }, 0) == 0 ? overflow::Constrain : overflow::Reject;
    }

    disambiguation get_disambiguation_option(value aOptions)
    {
        return static_cast<disambiguation>(get_string_option(aOptions, "disambiguation", { "compatible", "earlier", "later", "reject" }, 0));
    }

    offset_option get_offset_option(value aOptions, offset_option aFallback)
    {
        return static_cast<offset_option>(get_string_option(aOptions, "offset", { "prefer", "use", "ignore", "reject" }, static_cast<int>(aFallback)));
    }

    show_calendar get_show_calendar_option(value aOptions)
    {
        return static_cast<show_calendar>(get_string_option(aOptions, "calendarName", { "auto", "always", "never", "critical" }, 0));
    }

    show_time_zone get_show_time_zone_option(value aOptions)
    {
        return static_cast<show_time_zone>(get_string_option(aOptions, "timeZoneName", { "auto", "never", "critical" }, 0));
    }

    bool get_show_offset_option(value aOptions)
    {
        return get_string_option(aOptions, "offset", { "auto", "never" }, 0) == 0;
    }

    int get_fractional_second_digits_option(value aOptions)
    {
        auto const v = get(aOptions, ascii("fractionalSecondDigits"));
        if (is_undefined(v))
            return -1;
        if (!is_number(v))
        {
            if (str(to_string(v)) != u"auto")
                throw_range_error("fractionalSecondDigits must be auto or 0 to 9");
            return -1;
        }
        auto const n = as_number(v);
        if (!std::isfinite(n))
            throw_range_error("fractionalSecondDigits must be finite");
        auto const digits = std::floor(n);
        if (digits < 0.0 || digits > 9.0)
            throw_range_error("fractionalSecondDigits must be 0 to 9");
        return static_cast<int>(digits);
    }

    rounding_mode get_rounding_mode_option(value aOptions, rounding_mode aFallback)
    {
        return static_cast<rounding_mode>(get_string_option(aOptions, "roundingMode",
            { "ceil", "floor", "expand", "trunc", "halfCeil", "halfFloor", "halfExpand", "halfTrunc", "halfEven" }, static_cast<int>(aFallback)));
    }

    std::int64_t get_rounding_increment_option(value aOptions)
    {
        auto const v = get(aOptions, ascii("roundingIncrement"));
        if (is_undefined(v))
            return 1;
        auto const increment = to_integer_with_truncation(v);
        if (increment < 1.0 || increment > 1.0e9)
            throw_range_error("roundingIncrement must be 1 to 10^9");
        return static_cast<std::int64_t>(increment);
    }

    unit get_unit_option(value aOptions, char const* aKey, bool aRequired)
    {
        auto const v = get(aOptions, ascii(aKey));
        if (is_undefined(v))
        {
            if (aRequired)
                throw_range_error(std::string{ aKey } + " is required");
            return unit::Unset;
        }
        auto const& text = str(to_string(v));
        for (int i = 0; i < 10; ++i)
            if (equals_ascii(text, sUnitSingular[i]) || equals_ascii(text, sUnitPlural[i]))
                return static_cast<unit>(i);
        if (equals_ascii(text, "auto"))
            return unit::Auto;
        throw_range_error(utf8(make_string(text)) + " is not a valid value for " + aKey);
    }

    void validate_unit(unit aUnit, unit_group aGroup, std::initializer_list<unit> aExtra)
    {
        if (aUnit == unit::Unset)
            return;
        for (auto const e : aExtra)
            if (e == aUnit)
                return;
        if (aUnit == unit::Auto)
            throw_range_error("auto is not allowed here");
        if (is_date_unit(aUnit) && (aGroup == unit_group::Date || aGroup == unit_group::DateTime))
            return;
        if (!is_date_unit(aUnit) && (aGroup == unit_group::Time || aGroup == unit_group::DateTime))
            return;
        throw_range_error(std::string{ sUnitSingular[static_cast<int>(aUnit)] } + " is not allowed here");
    }

    void validate_rounding_increment(std::int64_t aIncrement, std::int64_t aDividend, bool aInclusive)
    {
        auto const maximum = aInclusive ? aDividend : aDividend - 1;
        if (aIncrement > maximum)
            throw_range_error("roundingIncrement is out of range");
        if (aDividend % aIncrement != 0)
            throw_range_error("roundingIncrement must divide the next larger unit");
    }

    rounding_mode negate_rounding_mode(rounding_mode aMode)
    {
        switch (aMode)
        {
        case rounding_mode::Ceil: return rounding_mode::Floor;
        case rounding_mode::Floor: return rounding_mode::Ceil;
        case rounding_mode::HalfCeil: return rounding_mode::HalfFloor;
        case rounding_mode::HalfFloor: return rounding_mode::HalfCeil;
        default: return aMode;
        }
    }

    double to_integer_with_truncation(value aValue)
    {
        auto const n = to_number(aValue);
        if (!std::isfinite(n))
            throw_range_error("value must be a finite number");
        return std::trunc(n) + 0.0;
    }

    double to_positive_integer_with_truncation(value aValue)
    {
        auto const n = to_integer_with_truncation(aValue);
        if (n <= 0.0)
            throw_range_error("value must be positive");
        return n;
    }

    double to_integer_if_integral(value aValue)
    {
        auto const n = to_number(aValue);
        if (!std::isfinite(n) || std::trunc(n) != n)
            throw_range_error("value must be an integer");
        return n + 0.0;
    }

    precision_record to_seconds_string_precision(unit aSmallestUnit, int aDigits)
    {
        switch (aSmallestUnit)
        {
        case unit::Minute: return { -2, unit::Minute, 1 };
        case unit::Second: return { 0, unit::Second, 1 };
        case unit::Millisecond: return { 3, unit::Millisecond, 1 };
        case unit::Microsecond: return { 6, unit::Microsecond, 1 };
        case unit::Nanosecond: return { 9, unit::Nanosecond, 1 };
        default: break;
        }
        if (aDigits < 0)
            return { -1, unit::Nanosecond, 1 };
        if (aDigits == 0)
            return { 0, unit::Second, 1 };
        static std::int64_t const sPowers[] = { 1, 10, 100, 1000 };
        if (aDigits <= 3)
            return { aDigits, unit::Millisecond, sPowers[3 - aDigits] };
        if (aDigits <= 6)
            return { aDigits, unit::Microsecond, sPowers[6 - aDigits] };
        return { aDigits, unit::Nanosecond, sPowers[9 - aDigits] };
    }

    difference_settings get_difference_settings(bool aSince, value aOptions, unit_group aGroup, std::initializer_list<unit> aDisallowed,
        unit aFallbackSmallest, unit aSmallestLargestDefault)
    {
        auto largestUnit = get_unit_option(aOptions, "largestUnit", false);
        auto const roundingIncrement = get_rounding_increment_option(aOptions);
        auto roundingMode = get_rounding_mode_option(aOptions, rounding_mode::Trunc);
        auto smallestUnit = get_unit_option(aOptions, "smallestUnit", false);
        validate_unit(largestUnit, aGroup, { unit::Auto });
        if (largestUnit == unit::Unset)
            largestUnit = unit::Auto;
        for (auto const d : aDisallowed)
            if (d == largestUnit)
                throw_range_error("largestUnit is not allowed here");
        validate_unit(smallestUnit, aGroup);
        if (smallestUnit == unit::Unset)
            smallestUnit = aFallbackSmallest;
        for (auto const d : aDisallowed)
            if (d == smallestUnit)
                throw_range_error("smallestUnit is not allowed here");
        auto const defaultLargestUnit = larger_unit(aSmallestLargestDefault, smallestUnit);
        if (largestUnit == unit::Auto)
            largestUnit = defaultLargestUnit;
        if (larger_unit(largestUnit, smallestUnit) != largestUnit)
            throw_range_error("largestUnit must not be smaller than smallestUnit");
        auto const maximum = maximum_rounding_increment(smallestUnit);
        if (maximum != 0)
            validate_rounding_increment(roundingIncrement, maximum, false);
        if (aSince)
            roundingMode = negate_rounding_mode(roundingMode);
        return { smallestUnit, largestUnit, roundingMode, roundingIncrement };
    }

    int unsigned_round(int aCompareHalf, bool aR1Even, rounding_mode aMode, bool aNegative)
    {
        enum class um { Zero, Infinity, HalfZero, HalfInfinity, HalfEven };
        um mode = um::Zero;
        switch (aMode)
        {
        case rounding_mode::Ceil: mode = aNegative ? um::Zero : um::Infinity; break;
        case rounding_mode::Floor: mode = aNegative ? um::Infinity : um::Zero; break;
        case rounding_mode::Expand: mode = um::Infinity; break;
        case rounding_mode::Trunc: mode = um::Zero; break;
        case rounding_mode::HalfCeil: mode = aNegative ? um::HalfZero : um::HalfInfinity; break;
        case rounding_mode::HalfFloor: mode = aNegative ? um::HalfInfinity : um::HalfZero; break;
        case rounding_mode::HalfExpand: mode = um::HalfInfinity; break;
        case rounding_mode::HalfTrunc: mode = um::HalfZero; break;
        case rounding_mode::HalfEven: mode = um::HalfEven; break;
        }
        if (mode == um::Zero)
            return 0;
        if (mode == um::Infinity)
            return 1;
        if (aCompareHalf < 0)
            return 0;
        if (aCompareHalf > 0)
            return 1;
        if (mode == um::HalfZero)
            return 0;
        if (mode == um::HalfInfinity)
            return 1;
        return aR1Even ? 0 : 1;
    }

    i128 round_to_increment(i128 const& aValue, i128 const& aIncrement, rounding_mode aMode)
    {
        bool const negative = aValue.negative();
        auto const absolute = aValue.abs();
        i128 q;
        i128 r;
        divmod(absolute, aIncrement, q, r);
        if (r.zero())
            return aValue;
        auto const half = compare(r + r, aIncrement);
        auto const rounded = q + i128{ unsigned_round(half, (q.lo() & 1u) == 0u, aMode, negative) };
        auto const result = rounded * aIncrement;
        return negative ? -result : result;
    }

    i128 round_to_increment_as_if_positive(i128 const& aValue, i128 const& aIncrement, rounding_mode aMode)
    {
        auto const q = floor_div(aValue, aIncrement);
        auto const r = aValue - q * aIncrement;
        if (r.zero())
            return aValue;
        auto const half = compare(r + r, aIncrement);
        return (q + i128{ unsigned_round(half, (q.lo() & 1u) == 0u, aMode, false) }) * aIncrement;
    }

    // --- dates and times

    namespace
    {
        constexpr std::int64_t sMonthStart[2][13] =
        {
            { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 },
            { 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366 }
        };

        constexpr std::int64_t fdiv(std::int64_t a, std::int64_t b)
        {
            auto const q = a / b;
            return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
        }

        constexpr std::int64_t fmod64(std::int64_t a, std::int64_t b)
        {
            return a - fdiv(a, b) * b;
        }

        std::int64_t day_from_year(std::int64_t aYear)
        {
            return 365 * (aYear - 1970) + fdiv(aYear - 1969, 4) - fdiv(aYear - 1901, 100) + fdiv(aYear - 1601, 400);
        }

        i128 big(std::int64_t aValue)
        {
            return i128{ aValue };
        }
    }

    std::int64_t clamp_int(double aValue)
    {
        if (aValue > 1.0e12)
            return 1000000000000LL;
        if (aValue < -1.0e12)
            return -1000000000000LL;
        return static_cast<std::int64_t>(aValue);
    }

    bool is_leap_year(std::int64_t aYear)
    {
        return fmod64(aYear, 4) == 0 && (fmod64(aYear, 100) != 0 || fmod64(aYear, 400) == 0);
    }

    int iso_days_in_month(std::int64_t aYear, int aMonth)
    {
        auto const& starts = sMonthStart[is_leap_year(aYear) ? 1 : 0];
        return static_cast<int>(starts[aMonth] - starts[aMonth - 1]);
    }

    std::int64_t iso_date_to_epoch_days(std::int64_t aYear, std::int64_t aMonth0, std::int64_t aDay)
    {
        auto const year = aYear + fdiv(aMonth0, 12);
        auto const month = fmod64(aMonth0, 12);
        return day_from_year(year) + sMonthStart[is_leap_year(year) ? 1 : 0][month] + aDay - 1;
    }

    iso_date epoch_days_to_iso_date(std::int64_t aDays)
    {
        auto year = 1970 + static_cast<std::int64_t>(std::floor(static_cast<double>(aDays) / 365.2425));
        while (day_from_year(year) > aDays)
            --year;
        while (day_from_year(year + 1) <= aDays)
            ++year;
        auto const dayInYear = aDays - day_from_year(year);
        auto const& starts = sMonthStart[is_leap_year(year) ? 1 : 0];
        int month = 0;
        while (month < 11 && dayInYear >= starts[month + 1])
            ++month;
        return { year, month + 1, static_cast<int>(dayInYear - starts[month]) + 1 };
    }

    bool is_valid_iso_date(std::int64_t aYear, std::int64_t aMonth, std::int64_t aDay)
    {
        if (aMonth < 1 || aMonth > 12)
            return false;
        return aDay >= 1 && aDay <= iso_days_in_month(aYear, static_cast<int>(aMonth));
    }

    bool is_valid_time(double h, double mi, double s, double ms, double us, double ns)
    {
        return h >= 0.0 && h <= 23.0 && mi >= 0.0 && mi <= 59.0 && s >= 0.0 && s <= 59.0 && ms >= 0.0 && ms <= 999.0 && us >= 0.0 && us <= 999.0 &&
            ns >= 0.0 && ns <= 999.0;
    }

    iso_date add_days_to_iso_date(iso_date const& aDate, std::int64_t aDays)
    {
        return epoch_days_to_iso_date(iso_date_to_epoch_days(aDate.year, aDate.month - 1, aDate.day) + aDays);
    }

    std::pair<std::int64_t, int> balance_iso_year_month(std::int64_t aYear, std::int64_t aMonth)
    {
        return { aYear + fdiv(aMonth - 1, 12), static_cast<int>(fmod64(aMonth - 1, 12)) + 1 };
    }

    iso_date regulate_iso_date(std::int64_t aYear, std::int64_t aMonth, std::int64_t aDay, overflow aOverflow)
    {
        if (aOverflow == overflow::Constrain)
        {
            auto const month = static_cast<int>(std::clamp<std::int64_t>(aMonth, 1, 12));
            auto const day = static_cast<int>(std::clamp<std::int64_t>(aDay, 1, iso_days_in_month(aYear, month)));
            return { aYear, month, day };
        }
        if (!is_valid_iso_date(aYear, aMonth, aDay))
            throw_range_error("invalid ISO date");
        return { aYear, static_cast<int>(aMonth), static_cast<int>(aDay) };
    }

    time_record regulate_time(double h, double mi, double s, double ms, double us, double ns, overflow aOverflow)
    {
        if (aOverflow == overflow::Constrain)
        {
            auto const c = [](double aValue, double aMax) { return static_cast<int>(std::clamp(aValue, 0.0, aMax)); };
            time_record t;
            t.hour = c(h, 23.0);
            t.minute = c(mi, 59.0);
            t.second = c(s, 59.0);
            t.millisecond = c(ms, 999.0);
            t.microsecond = c(us, 999.0);
            t.nanosecond = c(ns, 999.0);
            return t;
        }
        if (!is_valid_time(h, mi, s, ms, us, ns))
            throw_range_error("invalid time");
        time_record t;
        t.hour = static_cast<int>(h);
        t.minute = static_cast<int>(mi);
        t.second = static_cast<int>(s);
        t.millisecond = static_cast<int>(ms);
        t.microsecond = static_cast<int>(us);
        t.nanosecond = static_cast<int>(ns);
        return t;
    }

    namespace
    {
        time_record time_from_ns_of_day(i128 const& aTotal)
        {
            auto const days = floor_div(aTotal, big(nsPerDay));
            auto rem = (aTotal - days * big(nsPerDay)).to_int64();
            time_record t;
            t.days = days.to_int64();
            t.nanosecond = static_cast<int>(rem % 1000);
            rem /= 1000;
            t.microsecond = static_cast<int>(rem % 1000);
            rem /= 1000;
            t.millisecond = static_cast<int>(rem % 1000);
            rem /= 1000;
            t.second = static_cast<int>(rem % 60);
            rem /= 60;
            t.minute = static_cast<int>(rem % 60);
            rem /= 60;
            t.hour = static_cast<int>(rem);
            return t;
        }
    }

    time_record balance_time(i128 aHour, i128 aMinute, i128 aSecond, i128 aMillisecond, i128 aMicrosecond, i128 aNanosecond)
    {
        auto const total = ((((aHour * big(60) + aMinute) * big(60) + aSecond) * big(1000) + aMillisecond) * big(1000) + aMicrosecond) * big(1000) +
            aNanosecond;
        return time_from_ns_of_day(total);
    }

    i128 time_to_ns(time_record const& aTime)
    {
        return big(((((static_cast<std::int64_t>(aTime.hour) * 60 + aTime.minute) * 60 + aTime.second) * 1000 + aTime.millisecond) * 1000 +
            aTime.microsecond) * 1000 + aTime.nanosecond);
    }

    time_record add_time(time_record const& aTime, i128 const& aTimeDuration)
    {
        return time_from_ns_of_day(time_to_ns(aTime) + aTimeDuration);
    }

    time_record round_time(time_record const& aTime, std::int64_t aIncrement, unit aUnit, rounding_mode aMode)
    {
        auto const& t = aTime;
        std::int64_t quantity = 0;
        switch (aUnit)
        {
        case unit::Day:
        case unit::Hour:
            quantity = time_to_ns(t).to_int64();
            break;
        case unit::Minute:
            quantity = ((((static_cast<std::int64_t>(t.minute) * 60 + t.second) * 1000 + t.millisecond) * 1000 + t.microsecond) * 1000) + t.nanosecond;
            break;
        case unit::Second:
            quantity = (((static_cast<std::int64_t>(t.second) * 1000 + t.millisecond) * 1000 + t.microsecond) * 1000) + t.nanosecond;
            break;
        case unit::Millisecond:
            quantity = ((static_cast<std::int64_t>(t.millisecond) * 1000 + t.microsecond) * 1000) + t.nanosecond;
            break;
        case unit::Microsecond:
            quantity = static_cast<std::int64_t>(t.microsecond) * 1000 + t.nanosecond;
            break;
        default:
            quantity = t.nanosecond;
            break;
        }
        auto const length = unit_length(aUnit);
        auto const result = round_to_increment(big(quantity), big(aIncrement) * length, aMode) / length;
        switch (aUnit)
        {
        case unit::Day:
        {
            time_record r;
            r.days = result.to_int64();
            return r;
        }
        case unit::Hour: return balance_time(result, 0, 0, 0, 0, 0);
        case unit::Minute: return balance_time(t.hour, result, 0, 0, 0, 0);
        case unit::Second: return balance_time(t.hour, t.minute, result, 0, 0, 0);
        case unit::Millisecond: return balance_time(t.hour, t.minute, t.second, result, 0, 0);
        case unit::Microsecond: return balance_time(t.hour, t.minute, t.second, t.millisecond, result, 0);
        default: return balance_time(t.hour, t.minute, t.second, t.millisecond, t.microsecond, result);
        }
    }

    i128 difference_time(time_record const& a, time_record const& b)
    {
        return time_to_ns(b) - time_to_ns(a);
    }

    int compare_iso_date(iso_date const& a, iso_date const& b)
    {
        if (a.year != b.year)
            return a.year > b.year ? 1 : -1;
        if (a.month != b.month)
            return a.month > b.month ? 1 : -1;
        if (a.day != b.day)
            return a.day > b.day ? 1 : -1;
        return 0;
    }

    int compare_time(time_record const& a, time_record const& b)
    {
        return compare(time_to_ns(a), time_to_ns(b));
    }

    int compare_iso_date_time(iso_date_time const& a, iso_date_time const& b)
    {
        auto const d = compare_iso_date(a.date, b.date);
        return d != 0 ? d : compare_time(a.time, b.time);
    }

    iso_date_time balance_iso_date_time(iso_date const& aDate, i128 aHour, i128 aMinute, i128 aSecond, i128 aMs, i128 aUs, i128 aNs)
    {
        auto const t = balance_time(aHour, aMinute, aSecond, aMs, aUs, aNs);
        return { add_days_to_iso_date(aDate, t.days), t };
    }

    i128 get_utc_epoch_ns(iso_date_time const& aDateTime)
    {
        auto const days = iso_date_to_epoch_days(aDateTime.date.year, aDateTime.date.month - 1, aDateTime.date.day);
        return big(days) * big(nsPerDay) + time_to_ns(aDateTime.time);
    }

    iso_date_time iso_date_time_from_epoch_ns(i128 const& aEpochNs)
    {
        auto const days = floor_div(aEpochNs, big(nsPerDay));
        auto const rem = aEpochNs - days * big(nsPerDay);
        auto t = time_from_ns_of_day(rem);
        t.days = 0;
        return { epoch_days_to_iso_date(days.to_int64()), t };
    }

    bool iso_date_time_within_limits(iso_date_time const& aDateTime)
    {
        auto const days = iso_date_to_epoch_days(aDateTime.date.year, aDateTime.date.month - 1, aDateTime.date.day);
        if (days > 100000001 || days < -100000001)
            return false;
        auto const ns = get_utc_epoch_ns(aDateTime);
        if (ns <= -ns_max_instant() - big(nsPerDay))
            return false;
        if (ns >= ns_max_instant() + big(nsPerDay))
            return false;
        return true;
    }

    bool iso_date_within_limits(iso_date const& aDate)
    {
        time_record noon;
        noon.hour = 12;
        return iso_date_time_within_limits({ aDate, noon });
    }

    bool iso_year_month_within_limits(iso_date const& aDate)
    {
        if (aDate.year < -271821 || aDate.year > 275760)
            return false;
        if (aDate.year == -271821 && aDate.month < 4)
            return false;
        if (aDate.year == 275760 && aDate.month > 9)
            return false;
        return true;
    }

    void check_iso_days_range(iso_date const& aDate)
    {
        auto const days = iso_date_to_epoch_days(aDate.year, aDate.month - 1, aDate.day);
        if (days > 100000000 || days < -100000000)
            throw_range_error("date is outside the representable range");
    }

    bool is_valid_epoch_ns(i128 const& aEpochNs)
    {
        return aEpochNs >= -ns_max_instant() && aEpochNs <= ns_max_instant();
    }

    iso_date_time round_iso_date_time(iso_date_time const& aDateTime, std::int64_t aIncrement, unit aUnit, rounding_mode aMode)
    {
        auto const rounded = round_time(aDateTime.time, aIncrement, aUnit, aMode);
        return { add_days_to_iso_date(aDateTime.date, rounded.days), rounded };
    }

    int iso_day_of_week(iso_date const& aDate)
    {
        auto const days = iso_date_to_epoch_days(aDate.year, aDate.month - 1, aDate.day);
        auto const weekDay = static_cast<int>(fmod64(days + 4, 7));
        return weekDay == 0 ? 7 : weekDay;
    }

    int iso_day_of_year(iso_date const& aDate)
    {
        return static_cast<int>(sMonthStart[is_leap_year(aDate.year) ? 1 : 0][aDate.month - 1]) + aDate.day;
    }

    std::pair<int, std::int64_t> iso_week_of_year(iso_date const& aDate)
    {
        auto const year = aDate.year;
        auto const dayOfYear = iso_day_of_year(aDate);
        auto const dayOfWeek = iso_day_of_week(aDate);
        auto const week = static_cast<int>(fdiv(dayOfYear + 7 - dayOfWeek + 3, 7));
        if (week < 1)
        {
            auto const dayOfJan1st = iso_day_of_week({ year, 1, 1 });
            if (dayOfJan1st == 5)
                return { 53, year - 1 };
            if (dayOfJan1st == 6 && is_leap_year(year - 1))
                return { 53, year - 1 };
            return { 52, year - 1 };
        }
        if (week == 53)
        {
            auto const daysInYear = is_leap_year(year) ? 366 : 365;
            if (daysInYear - dayOfYear < 4 - dayOfWeek)
                return { 1, year + 1 };
        }
        return { week, year };
    }

    // --- formatting

    std::string zero_pad(std::int64_t aValue, int aWidth)
    {
        auto text = std::to_string(aValue);
        if (static_cast<int>(text.size()) < aWidth)
            text.insert(0u, static_cast<std::size_t>(aWidth) - text.size(), '0');
        return text;
    }

    std::string pad_iso_year(std::int64_t aYear)
    {
        if (aYear >= 0 && aYear <= 9999)
            return zero_pad(aYear, 4);
        return (aYear > 0 ? "+" : "-") + zero_pad(aYear < 0 ? -aYear : aYear, 6);
    }

    std::string format_fractional_seconds(std::int64_t aSubSecondNs, int aPrecision)
    {
        std::string fraction;
        if (aPrecision == -1)
        {
            if (aSubSecondNs == 0)
                return {};
            fraction = zero_pad(aSubSecondNs, 9);
            while (!fraction.empty() && fraction.back() == '0')
                fraction.pop_back();
        }
        else
        {
            if (aPrecision == 0)
                return {};
            fraction = zero_pad(aSubSecondNs, 9).substr(0u, static_cast<std::size_t>(aPrecision));
        }
        return "." + fraction;
    }

    std::string format_time_string(int aHour, int aMinute, int aSecond, std::int64_t aSubSecondNs, int aPrecision, bool aSeparated)
    {
        std::string const separator = aSeparated ? ":" : "";
        auto result = zero_pad(aHour, 2) + separator + zero_pad(aMinute, 2);
        if (aPrecision == -2)
            return result;
        return result + separator + zero_pad(aSecond, 2) + format_fractional_seconds(aSubSecondNs, aPrecision);
    }

    std::string time_record_to_string(time_record const& aTime, int aPrecision)
    {
        auto const subSecond = static_cast<std::int64_t>(aTime.millisecond) * 1000000 + static_cast<std::int64_t>(aTime.microsecond) * 1000 +
            aTime.nanosecond;
        return format_time_string(aTime.hour, aTime.minute, aTime.second, subSecond, aPrecision);
    }

    std::string iso_date_to_string(iso_date const& aDate)
    {
        return pad_iso_year(aDate.year) + "-" + zero_pad(aDate.month, 2) + "-" + zero_pad(aDate.day, 2);
    }

    std::string format_calendar_annotation(calendar_id aCalendar, show_calendar aShowCalendar)
    {
        if (aShowCalendar == show_calendar::Never || (aShowCalendar == show_calendar::Auto && aCalendar == calendar_id::Iso8601))
            return {};
        return std::string{ aShowCalendar == show_calendar::Critical ? "[!u-ca=" : "[u-ca=" } + calendar_identifier(aCalendar) + "]";
    }

    std::string iso_date_time_to_string(iso_date_time const& aDateTime, int aPrecision, calendar_id aCalendar, show_calendar aShowCalendar)
    {
        return iso_date_to_string(aDateTime.date) + "T" + time_record_to_string(aDateTime.time, aPrecision) +
            format_calendar_annotation(aCalendar, aShowCalendar);
    }

    std::string format_offset_minutes(int aOffsetMinutes, bool aSeparated)
    {
        auto const absolute = aOffsetMinutes < 0 ? -aOffsetMinutes : aOffsetMinutes;
        return (aOffsetMinutes >= 0 ? "+" : "-") + format_time_string(absolute / 60, absolute % 60, 0, 0, -2, aSeparated);
    }

    std::string format_utc_offset_ns(std::int64_t aOffsetNs)
    {
        auto const absolute = aOffsetNs < 0 ? -aOffsetNs : aOffsetNs;
        auto const hour = static_cast<int>(absolute / 3600000000000LL);
        auto const minute = static_cast<int>((absolute / 60000000000LL) % 60);
        auto const second = static_cast<int>((absolute / 1000000000LL) % 60);
        auto const subSecond = absolute % 1000000000LL;
        auto const precision = (second == 0 && subSecond == 0) ? -2 : -1;
        return (aOffsetNs >= 0 ? "+" : "-") + format_time_string(hour, minute, second, subSecond, precision);
    }

    std::string format_utc_offset_rounded(std::int64_t aOffsetNs)
    {
        auto const rounded = round_to_increment(big(aOffsetNs), big(60000000000LL), rounding_mode::HalfExpand);
        return format_offset_minutes(static_cast<int>((rounded / big(60000000000LL)).to_int64()));
    }

    // --- durations

    bool is_valid_duration(duration_fields const& aFields)
    {
        int sign = 0;
        for (auto const v : aFields.v)
        {
            if (!std::isfinite(v))
                return false;
            if (v < 0.0)
            {
                if (sign > 0)
                    return false;
                sign = -1;
            }
            else if (v > 0.0)
            {
                if (sign < 0)
                    return false;
                sign = 1;
            }
        }
        auto const limit32 = 4294967296.0;
        if (std::fabs(aFields.v[0]) >= limit32 || std::fabs(aFields.v[1]) >= limit32 || std::fabs(aFields.v[2]) >= limit32)
            return false;
        static double const sUnitNs[] = { 86400.0e9, 3600.0e9, 60.0e9, 1.0e9, 1.0e6, 1.0e3, 1.0 };
        double const limit = 9007199254740992.0e9;
        for (int i = 0; i < 7; ++i)
            if (std::fabs(aFields.v[3 + i]) * sUnitNs[i] > 2.0 * limit)
                return false;
        static std::int64_t const sUnitNsExact[] = { nsPerDay, 3600000000000LL, 60000000000LL, 1000000000LL, 1000000LL, 1000LL, 1LL };
        i128 total;
        for (int i = 0; i < 7; ++i)
            total += i128::from_double(aFields.v[3 + i]) * big(sUnitNsExact[i]);
        return total.abs() <= max_time_duration();
    }

    int duration_sign(duration_fields const& aFields)
    {
        for (auto const v : aFields.v)
        {
            if (v < 0.0)
                return -1;
            if (v > 0.0)
                return 1;
        }
        return 0;
    }

    int date_duration_sign(date_duration const& aDuration)
    {
        for (auto const v : { aDuration.years, aDuration.months, aDuration.weeks, aDuration.days })
        {
            if (v < 0.0)
                return -1;
            if (v > 0.0)
                return 1;
        }
        return 0;
    }

    int internal_duration_sign(internal_duration const& aDuration)
    {
        auto const d = date_duration_sign(aDuration.date);
        return d != 0 ? d : aDuration.time.sign();
    }

    date_duration create_date_duration(double aYears, double aMonths, double aWeeks, double aDays)
    {
        duration_fields f;
        f.v[0] = aYears;
        f.v[1] = aMonths;
        f.v[2] = aWeeks;
        f.v[3] = aDays;
        if (!is_valid_duration(f))
            throw_range_error("invalid duration");
        return { aYears + 0.0, aMonths + 0.0, aWeeks + 0.0, aDays + 0.0 };
    }

    unit default_largest_unit(duration_fields const& aFields)
    {
        for (int i = 0; i < 9; ++i)
            if (aFields.v[i] != 0.0)
                return static_cast<unit>(i);
        return unit::Nanosecond;
    }

    i128 time_duration_from_components(double h, double mi, double s, double ms, double us, double ns)
    {
        return ((((i128::from_double(h) * big(60) + i128::from_double(mi)) * big(60) + i128::from_double(s)) * big(1000) + i128::from_double(ms)) *
            big(1000) + i128::from_double(us)) * big(1000) + i128::from_double(ns);
    }

    i128 add_time_duration(i128 const& a, i128 const& b)
    {
        auto const result = a + b;
        if (result.abs() > max_time_duration())
            throw_range_error("duration is out of range");
        return result;
    }

    i128 add_24_hour_days(i128 const& aTime, double aDays)
    {
        auto const result = aTime + i128::from_double(aDays) * big(nsPerDay);
        if (result.abs() > max_time_duration())
            throw_range_error("duration is out of range");
        return result;
    }

    internal_duration to_internal_duration(duration_fields const& aFields)
    {
        auto const& v = aFields.v;
        return { { v[0], v[1], v[2], v[3] }, time_duration_from_components(v[4], v[5], v[6], v[7], v[8], v[9]) };
    }

    internal_duration to_internal_duration_24(duration_fields const& aFields)
    {
        auto const& v = aFields.v;
        auto const time = time_duration_from_components(v[4], v[5], v[6], v[7], v[8], v[9]) + i128::from_double(v[3]) * big(nsPerDay);
        return { { v[0], v[1], v[2], 0.0 }, time };
    }

    date_duration to_date_duration_without_time(duration_fields const& aFields)
    {
        auto const internal = to_internal_duration_24(aFields);
        auto const days = (internal.time / big(nsPerDay)).to_double();
        return { internal.date.years, internal.date.months, internal.date.weeks, days + 0.0 };
    }

    duration_fields duration_from_internal(internal_duration const& aDuration, unit aLargestUnit)
    {
        i128 days;
        i128 hours;
        i128 minutes;
        i128 seconds;
        i128 milliseconds;
        i128 microseconds;
        auto const sign = aDuration.time.sign();
        auto nanoseconds = aDuration.time.abs();
        auto const step = [](i128& aSmall, i128& aLarge, std::int64_t aFactor)
        {
            i128 q;
            i128 r;
            divmod(aSmall, big(aFactor), q, r);
            aLarge = q;
            aSmall = r;
        };
        if (is_date_unit(aLargestUnit) || aLargestUnit == unit::Hour || aLargestUnit == unit::Minute || aLargestUnit == unit::Second ||
            aLargestUnit == unit::Millisecond || aLargestUnit == unit::Microsecond)
            step(nanoseconds, microseconds, 1000);
        if (is_date_unit(aLargestUnit) || aLargestUnit == unit::Hour || aLargestUnit == unit::Minute || aLargestUnit == unit::Second ||
            aLargestUnit == unit::Millisecond)
            step(microseconds, milliseconds, 1000);
        if (is_date_unit(aLargestUnit) || aLargestUnit == unit::Hour || aLargestUnit == unit::Minute || aLargestUnit == unit::Second)
            step(milliseconds, seconds, 1000);
        if (is_date_unit(aLargestUnit) || aLargestUnit == unit::Hour || aLargestUnit == unit::Minute)
            step(seconds, minutes, 60);
        if (is_date_unit(aLargestUnit) || aLargestUnit == unit::Hour)
            step(minutes, hours, 60);
        if (is_date_unit(aLargestUnit))
            step(hours, days, 24);
        auto const s = big(sign);
        duration_fields f;
        f.v[0] = aDuration.date.years;
        f.v[1] = aDuration.date.months;
        f.v[2] = aDuration.date.weeks;
        f.v[3] = (i128::from_double(aDuration.date.days) + days * s).to_double();
        f.v[4] = (hours * s).to_double();
        f.v[5] = (minutes * s).to_double();
        f.v[6] = (seconds * s).to_double();
        f.v[7] = (milliseconds * s).to_double();
        f.v[8] = (microseconds * s).to_double();
        f.v[9] = (nanoseconds * s).to_double();
        for (auto& v : f.v)
            v += 0.0;
        if (!is_valid_duration(f))
            throw_range_error("duration is out of range");
        return f;
    }

    duration_fields negate_duration(duration_fields const& aFields)
    {
        duration_fields f;
        for (int i = 0; i < 10; ++i)
            f.v[i] = -aFields.v[i] + 0.0;
        return f;
    }

    i128 round_time_duration(i128 const& aTime, std::int64_t aIncrement, unit aUnit, rounding_mode aMode)
    {
        auto const rounded = round_to_increment(aTime, unit_length(aUnit) * big(aIncrement), aMode);
        if (rounded.abs() > max_time_duration())
            throw_range_error("duration is out of range");
        return rounded;
    }

    double total_time_duration(i128 const& aTime, unit aUnit)
    {
        return ratio_to_double(aTime, unit_length(aUnit));
    }

    std::string duration_to_string(duration_fields const& aFields, int aPrecision)
    {
        auto const& v = aFields.v;
        auto const sign = duration_sign(aFields);
        auto const number = [](double aValue) { return i128::from_double(std::fabs(aValue)).to_string(); };
        std::string datePart;
        if (v[0] != 0.0)
            datePart += number(v[0]) + "Y";
        if (v[1] != 0.0)
            datePart += number(v[1]) + "M";
        if (v[2] != 0.0)
            datePart += number(v[2]) + "W";
        if (v[3] != 0.0)
            datePart += number(v[3]) + "D";
        std::string timePart;
        if (v[4] != 0.0)
            timePart += number(v[4]) + "H";
        if (v[5] != 0.0)
            timePart += number(v[5]) + "M";
        auto const largest = default_largest_unit(aFields);
        bool const zeroMinutesAndHigher = largest == unit::Second || largest == unit::Millisecond || largest == unit::Microsecond ||
            largest == unit::Nanosecond;
        auto const secondsDuration = time_duration_from_components(0.0, 0.0, v[6], v[7], v[8], v[9]);
        if (!secondsDuration.zero() || zeroMinutesAndHigher || aPrecision != -1)
        {
            i128 q;
            i128 r;
            divmod(secondsDuration, big(1000000000), q, r);
            timePart += q.abs().to_string() + format_fractional_seconds(r.abs().to_int64(), aPrecision) + "S";
        }
        auto result = std::string{ sign < 0 ? "-" : "" } + "P" + datePart;
        if (!timePart.empty())
            result += "T" + timePart;
        return result;
    }

    duration_fields to_partial_duration(value aObject, bool& aAny)
    {
        if (!is_object(aObject))
            throw_type_error("duration-like must be an object");
        duration_fields f;
        for (auto& v : f.v)
            v = std::numeric_limits<double>::quiet_NaN();
        aAny = false;
        // (alphabetical order)
        static struct { char const* name; int index; } const sOrder[] = {
            { "days", 3 }, { "hours", 4 }, { "microseconds", 8 }, { "milliseconds", 7 }, { "minutes", 5 }, { "months", 1 },
            { "nanoseconds", 9 }, { "seconds", 6 }, { "weeks", 2 }, { "years", 0 } };
        for (auto const& [name, index] : sOrder)
        {
            auto const v = get(aObject, ascii(name));
            if (!is_undefined(v))
            {
                f.v[index] = to_integer_if_integral(v);
                aAny = true;
            }
        }
        if (!aAny)
            throw_type_error("duration-like object must have at least one duration property");
        return f;
    }

    // --- calendars

    std::u16string ascii_lowercase(std::u16string aText)
    {
        for (auto& c : aText)
            if (c >= u'A' && c <= u'Z')
                c = static_cast<char16_t>(c + (u'a' - u'A'));
        return aText;
    }

    calendar_id canonicalize_calendar(value aId)
    {
        if (auto const calendar = available_calendar(str(aId)))
            return *calendar;
        throw_range_error("unsupported calendar: " + utf8(aId));
    }

    namespace
    {
        bool has_calendar(value aValue)
        {
            if (!is_object(aValue))
                return false;
            auto const cls = obj(aValue).cls;
            return cls == object_class::TemporalPlainDate || cls == object_class::TemporalPlainDateTime || cls == object_class::TemporalPlainMonthDay ||
                cls == object_class::TemporalPlainYearMonth || cls == object_class::TemporalZonedDateTime;
        }
    }

    calendar_id to_temporal_calendar_identifier(value aValue)
    {
        if (has_calendar(aValue))
            return tobj(aValue).calendar;
        if (!is_string(aValue))
            throw_type_error("calendar must be a string");
        auto const identifier = parse_temporal_calendar_string(str(aValue));
        return canonicalize_calendar(make_string(identifier));
    }

    calendar_id get_temporal_calendar_with_iso_default(value aItem)
    {
        if (has_calendar(aItem))
            return tobj(aItem).calendar;
        auto const calendarLike = get(aItem, ascii("calendar"));
        if (is_undefined(calendarLike))
            return calendar_id::Iso8601;
        return to_temporal_calendar_identifier(calendarLike);
    }

    calendar_fields prepare_calendar_fields(calendar_id aCalendar, value aObject, unsigned aFieldNames, unsigned aRequired, bool aPartial)
    {
        calendar_fields result;
        bool any = false;
        aFieldNames |= calendar_extra_fields(aCalendar, aFieldNames);
        // (lexicographic order of the property names)
        static struct { char const* name; unsigned key; } const sOrder[] = {
            { "day", FieldDay }, { "era", FieldEra }, { "eraYear", FieldEraYear }, { "hour", FieldHour }, { "microsecond", FieldMicrosecond },
            { "millisecond", FieldMillisecond },
            { "minute", FieldMinute }, { "month", FieldMonth }, { "monthCode", FieldMonthCode }, { "nanosecond", FieldNanosecond },
            { "offset", FieldOffset }, { "second", FieldSecond }, { "timeZone", FieldTimeZone }, { "year", FieldYear } };
        for (auto const& [name, key] : sOrder)
        {
            if ((aFieldNames & key) == 0u)
                continue;
            auto const v = get(aObject, ascii(name));
            if (!is_undefined(v))
            {
                any = true;
                switch (key)
                {
                case FieldYear: result.year = to_integer_with_truncation(v); break;
                case FieldEraYear: result.eraYear = to_integer_with_truncation(v); break;
                case FieldEra: result.era = str(to_string(v)); break;
                case FieldMonth: result.month = to_positive_integer_with_truncation(v); break;
                case FieldDay: result.day = to_positive_integer_with_truncation(v); break;
                case FieldHour: result.hour = to_integer_with_truncation(v); break;
                case FieldMinute: result.minute = to_integer_with_truncation(v); break;
                case FieldSecond: result.second = to_integer_with_truncation(v); break;
                case FieldMillisecond: result.millisecond = to_integer_with_truncation(v); break;
                case FieldMicrosecond: result.microsecond = to_integer_with_truncation(v); break;
                case FieldNanosecond: result.nanosecond = to_integer_with_truncation(v); break;
                case FieldMonthCode:
                {
                    auto const p = to_primitive(v, hint::String);
                    if (!is_string(p))
                        throw_type_error("monthCode must be a string");
                    int month = 0;
                    bool leap = false;
                    if (!parse_month_code(str(p), month, leap))
                        throw_range_error("invalid monthCode: " + utf8(p));
                    result.monthCode = u16("M" + zero_pad(month, 2) + (leap ? "L" : ""));
                    break;
                }
                case FieldOffset:
                {
                    auto const p = to_primitive(v, hint::String);
                    if (!is_string(p))
                        throw_type_error("offset must be a string");
                    if (!parse_date_time_utc_offset(str(p)))
                        throw_range_error("invalid offset: " + utf8(p));
                    result.offset = str(p);
                    break;
                }
                case FieldTimeZone:
                    result.timeZone = to_temporal_time_zone_identifier(v);
                    break;
                default:
                    break;
                }
            }
            else if (!aPartial)
            {
                if ((aRequired & key) != 0u)
                    throw_type_error(std::string{ name } + " is required");
                switch (key)
                {
                case FieldHour: result.hour = 0.0; break;
                case FieldMinute: result.minute = 0.0; break;
                case FieldSecond: result.second = 0.0; break;
                case FieldMillisecond: result.millisecond = 0.0; break;
                case FieldMicrosecond: result.microsecond = 0.0; break;
                case FieldNanosecond: result.nanosecond = 0.0; break;
                default: break;
                }
            }
        }
        if (aPartial && !any)
            throw_type_error("at least one recognized property is required");
        return result;
    }

    calendar_fields iso_date_to_fields(calendar_id aCalendar, iso_date const& aDate, int aType)
    {
        calendar_fields f;
        if (aCalendar == calendar_id::Iso8601)
        {
            f.monthCode = u16("M" + zero_pad(aDate.month, 2));
            if (aType == 0 || aType == 2)
                f.day = static_cast<double>(aDate.day);
            if (aType == 0 || aType == 1)
                f.year = static_cast<double>(aDate.year);
            return f;
        }
        auto const date = calendar_iso_to_date(aCalendar, aDate);
        f.monthCode = u16(date.month_code());
        if (aType == 0 || aType == 2)
            f.day = static_cast<double>(date.day);
        if (aType == 0 || aType == 1)
            f.year = static_cast<double>(date.year);
        return f;
    }

    calendar_fields merge_calendar_fields(calendar_id aCalendar, calendar_fields const& aFields, calendar_fields const& aAdditional)
    {
        auto merged = aFields;
        auto const mergeOne = [](auto& aTarget, auto const& aSource)
        {
            if (aSource)
                aTarget = aSource;
        };
        // (CalendarFieldKeysToIgnore: the fields the additional fields override)
        unsigned additionalKeys = 0u;
        if (aAdditional.year) additionalKeys |= FieldYear;
        if (aAdditional.month) additionalKeys |= FieldMonth;
        if (aAdditional.monthCode) additionalKeys |= FieldMonthCode;
        if (aAdditional.day) additionalKeys |= FieldDay;
        if (aAdditional.era) additionalKeys |= FieldEra;
        if (aAdditional.eraYear) additionalKeys |= FieldEraYear;
        auto const overridden = calendar_field_keys_to_ignore(aCalendar, additionalKeys);
        if ((overridden & FieldYear) != 0u) merged.year.reset();
        if ((overridden & FieldMonth) != 0u) merged.month.reset();
        if ((overridden & FieldMonthCode) != 0u) merged.monthCode.reset();
        if ((overridden & FieldDay) != 0u) merged.day.reset();
        if ((overridden & FieldEra) != 0u) merged.era.reset();
        if ((overridden & FieldEraYear) != 0u) merged.eraYear.reset();
        mergeOne(merged.era, aAdditional.era);
        mergeOne(merged.eraYear, aAdditional.eraYear);
        mergeOne(merged.year, aAdditional.year);
        mergeOne(merged.month, aAdditional.month);
        mergeOne(merged.monthCode, aAdditional.monthCode);
        mergeOne(merged.day, aAdditional.day);
        mergeOne(merged.hour, aAdditional.hour);
        mergeOne(merged.minute, aAdditional.minute);
        mergeOne(merged.second, aAdditional.second);
        mergeOne(merged.millisecond, aAdditional.millisecond);
        mergeOne(merged.microsecond, aAdditional.microsecond);
        mergeOne(merged.nanosecond, aAdditional.nanosecond);
        mergeOne(merged.offset, aAdditional.offset);
        mergeOne(merged.timeZone, aAdditional.timeZone);
        return merged;
    }

    namespace
    {
        void calendar_resolve_fields(calendar_id aCalendar, calendar_fields& aFields, int aType)   ///< (0 date, 1 year-month, 2 month-day)
        {
            if (aCalendar != calendar_id::Iso8601)
            {
                non_iso_resolve_fields(aCalendar, aFields, aType);
                return;
            }
            bool const needsYear = aType == 0 || aType == 1;
            bool const needsDay = aType == 0 || aType == 2;
            if (needsYear && !aFields.year)
                throw_type_error("year is required");
            if (needsDay && !aFields.day)
                throw_type_error("day is required");
            if (!aFields.month && !aFields.monthCode)
                throw_type_error("month or monthCode is required");
            if (aFields.monthCode)
            {
                int month = 0;
                bool leap = false;
                parse_month_code(*aFields.monthCode, month, leap);
                if (leap)
                    throw_range_error("the ISO 8601 calendar has no leap months");
                if (month > 12)
                    throw_range_error("invalid monthCode");
                if (aFields.month && *aFields.month != static_cast<double>(month))
                    throw_range_error("month and monthCode do not agree");
                aFields.month = static_cast<double>(month);
            }
        }
    }

    iso_date calendar_date_from_fields(calendar_id aCalendar, calendar_fields aFields, overflow aOverflow)
    {
        calendar_resolve_fields(aCalendar, aFields, 0);
        auto const result = aCalendar == calendar_id::Iso8601 ?
            regulate_iso_date(clamp_int(*aFields.year), clamp_int(*aFields.month), clamp_int(*aFields.day), aOverflow) :
            non_iso_calendar_date_to_iso(aCalendar, aFields, aOverflow);
        if (!iso_date_within_limits(result))
            throw_range_error("date is outside the representable range");
        return result;
    }

    iso_date calendar_year_month_from_fields(calendar_id aCalendar, calendar_fields aFields, overflow aOverflow)
    {
        aFields.day = 1.0;
        calendar_resolve_fields(aCalendar, aFields, 1);
        auto const result = aCalendar == calendar_id::Iso8601 ? regulate_iso_date(clamp_int(*aFields.year), clamp_int(*aFields.month), 1, aOverflow) :
            non_iso_calendar_date_to_iso(aCalendar, aFields, aOverflow);
        if (!iso_year_month_within_limits(result))
            throw_range_error("year-month is outside the representable range");
        return result;
    }

    iso_date calendar_month_day_from_fields(calendar_id aCalendar, calendar_fields aFields, overflow aOverflow)
    {
        calendar_resolve_fields(aCalendar, aFields, 2);
        if (aCalendar != calendar_id::Iso8601)
            return non_iso_month_day_to_iso_reference_date(aCalendar, aFields, aOverflow);
        auto const year = aFields.year ? clamp_int(*aFields.year) : 1972;
        auto const result = regulate_iso_date(year, clamp_int(*aFields.month), clamp_int(*aFields.day), aOverflow);
        return { 1972, result.month, result.day };
    }

    iso_date_time interpret_temporal_date_time_fields(calendar_id aCalendar, calendar_fields const& aFields, overflow aOverflow)
    {
        auto const date = calendar_date_from_fields(aCalendar, aFields, aOverflow);
        auto const time = regulate_time(aFields.hour.value_or(0.0), aFields.minute.value_or(0.0), aFields.second.value_or(0.0),
            aFields.millisecond.value_or(0.0), aFields.microsecond.value_or(0.0), aFields.nanosecond.value_or(0.0), aOverflow);
        return { date, time };
    }

    iso_date calendar_date_add(calendar_id aCalendar, iso_date const& aDate, date_duration const& aDuration, overflow aOverflow)
    {
        if (aCalendar != calendar_id::Iso8601)
            return non_iso_date_add(aCalendar, aDate, aDuration, aOverflow);
        auto const [year, month] = balance_iso_year_month(aDate.year + static_cast<std::int64_t>(aDuration.years),
            aDate.month + static_cast<std::int64_t>(aDuration.months));
        auto const intermediate = regulate_iso_date(year, month, aDate.day, aOverflow);
        auto const days = static_cast<std::int64_t>(aDuration.days) + 7 * static_cast<std::int64_t>(aDuration.weeks);
        auto const result = add_days_to_iso_date(intermediate, days);
        if (!iso_date_within_limits(result))
            throw_range_error("date is outside the representable range");
        return result;
    }

    namespace
    {
        // (CompareSurpasses with a month number)
        bool surpasses(int aSign, std::int64_t aYear, int aMonth, int aDay, iso_date const& aTarget)
        {
            if (aYear != aTarget.year)
                return aSign * (aYear > aTarget.year ? 1 : -1) > 0;
            if (aMonth != aTarget.month)
                return aSign * (aMonth > aTarget.month ? 1 : -1) > 0;
            if (aDay != aTarget.day)
                return aSign * (aDay > aTarget.day ? 1 : -1) > 0;
            return false;
        }
    }

    // (CalendarDateUntil for the ISO 8601 calendar: the specification's loops (one candidate year, month, week or day at a
    // time, with ISODateSurpasses) computed directly. Note: the current text of ISODateSurpasses returns false when months
    // is 0 before it looks at weeks and days, which would never end the days loop; the intended semantics (each larger
    // unit, then the regulated date plus weeks and days, must not surpass the target) are used.)
    date_duration calendar_date_until(calendar_id aCalendar, iso_date const& aOne, iso_date const& aTwo, unit aLargestUnit)
    {
        if (aCalendar != calendar_id::Iso8601)
            return non_iso_date_until(aCalendar, aOne, aTwo, aLargestUnit);
        auto sign = compare_iso_date(aOne, aTwo);
        if (sign == 0)
            return {};
        sign = -sign;
        std::int64_t years = 0;
        if (aLargestUnit == unit::Year)
        {
            auto candidate = aTwo.year - aOne.year;
            if (surpasses(sign, aOne.year + candidate, aOne.month, aOne.day, aTwo))
                candidate -= sign;
            if (sign * candidate > 0)
                years = candidate;
        }
        auto const y0 = aOne.year + years;
        std::int64_t months = 0;
        if (aLargestUnit == unit::Year || aLargestUnit == unit::Month)
        {
            auto candidate = (aTwo.year - y0) * 12 + (aTwo.month - aOne.month);
            auto const added = balance_iso_year_month(y0, aOne.month + candidate);
            if (surpasses(sign, added.first, added.second, aOne.day, aTwo))
                candidate -= sign;
            if (sign * candidate > 0)
                months = candidate;
        }
        auto const monthsAdded = balance_iso_year_month(y0, aOne.month + months);
        auto const regulated = regulate_iso_date(monthsAdded.first, monthsAdded.second, aOne.day, overflow::Constrain);
        auto const regulatedDays = iso_date_to_epoch_days(regulated.year, regulated.month - 1, regulated.day);
        auto const targetDays = iso_date_to_epoch_days(aTwo.year, aTwo.month - 1, aTwo.day);
        auto difference = targetDays - regulatedDays;
        if (sign * difference < 0)
            difference = 0;
        std::int64_t weeks = 0;
        if (aLargestUnit == unit::Week)
            weeks = difference / 7;
        auto const days = difference - weeks * 7;
        return { static_cast<double>(years), static_cast<double>(months), static_cast<double>(weeks), static_cast<double>(days) };
    }

    // --- relative durations

    namespace
    {
        struct nudge_result
        {
            internal_duration duration;
            i128 nudgedEpochNs;
            bool didExpandCalendarUnit = false;
        };

        struct nudge_window
        {
            double r1 = 0.0;
            double r2 = 0.0;
            i128 startEpochNs;
            i128 endEpochNs;
            internal_duration startDuration;
            internal_duration endDuration;
        };

        double round_number_trunc(double aValue, std::int64_t aIncrement)
        {
            auto const inc = static_cast<double>(aIncrement);
            return std::trunc(aValue / inc) * inc + 0.0;
        }

        i128 epoch_ns_for(iso_date_time const& aDateTime, time_zone const* aTimeZone)
        {
            if (aTimeZone == nullptr)
                return get_utc_epoch_ns(aDateTime);
            return get_epoch_ns_for(*aTimeZone, aDateTime, disambiguation::Compatible);
        }

        date_duration adjust_date_duration(date_duration const& aDuration, double aDays, std::optional<double> aWeeks = {},
            std::optional<double> aMonths = {})
        {
            return create_date_duration(aDuration.years, aMonths.value_or(aDuration.months), aWeeks.value_or(aDuration.weeks), aDays);
        }

        nudge_window compute_nudge_window(int aSign, internal_duration const& aDuration, i128 const& aOriginNs, iso_date_time const& aDateTime,
            calendar_id aCalendar, time_zone const* aTimeZone, std::int64_t aIncrement, unit aUnit, bool aAdditionalShift)
        {
            nudge_window w;
            auto const inc = static_cast<double>(aIncrement);
            date_duration startDate;
            date_duration endDate;
            if (aUnit == unit::Year)
            {
                auto const years = round_number_trunc(aDuration.date.years, aIncrement);
                w.r1 = aAdditionalShift ? years + inc * aSign : years;
                w.r2 = w.r1 + inc * aSign;
                startDate = create_date_duration(w.r1, 0.0, 0.0, 0.0);
                endDate = create_date_duration(w.r2, 0.0, 0.0, 0.0);
            }
            else if (aUnit == unit::Month)
            {
                auto const months = round_number_trunc(aDuration.date.months, aIncrement);
                w.r1 = aAdditionalShift ? months + inc * aSign : months;
                w.r2 = w.r1 + inc * aSign;
                startDate = adjust_date_duration(aDuration.date, 0.0, 0.0, w.r1);
                endDate = adjust_date_duration(aDuration.date, 0.0, 0.0, w.r2);
            }
            else if (aUnit == unit::Week)
            {
                auto const yearsMonths = adjust_date_duration(aDuration.date, 0.0, 0.0);
                auto const weeksStart = calendar_date_add(aCalendar, aDateTime.date, yearsMonths, overflow::Constrain);
                auto const weeksEnd = add_days_to_iso_date(weeksStart, static_cast<std::int64_t>(aDuration.date.days));
                auto const untilResult = calendar_date_until(aCalendar, weeksStart, weeksEnd, unit::Week);
                auto const weeks = round_number_trunc(aDuration.date.weeks + untilResult.weeks, aIncrement);
                w.r1 = weeks;
                w.r2 = weeks + inc * aSign;
                startDate = adjust_date_duration(aDuration.date, 0.0, w.r1);
                endDate = adjust_date_duration(aDuration.date, 0.0, w.r2);
            }
            else
            {
                auto const days = round_number_trunc(aDuration.date.days, aIncrement);
                w.r1 = days;
                w.r2 = days + inc * aSign;
                startDate = adjust_date_duration(aDuration.date, w.r1);
                endDate = adjust_date_duration(aDuration.date, w.r2);
            }
            // (the specification's condition is r1 = 0; but for weeks and months the start duration can still have larger
            // units (test262 expects them to be added): the origin is the start only when the start duration is zero)
            if (date_duration_sign(startDate) == 0)
                w.startEpochNs = aOriginNs;
            else
            {
                auto const start = calendar_date_add(aCalendar, aDateTime.date, startDate, overflow::Constrain);
                w.startEpochNs = epoch_ns_for({ start, aDateTime.time }, aTimeZone);
            }
            auto const end = calendar_date_add(aCalendar, aDateTime.date, endDate, overflow::Constrain);
            w.endEpochNs = epoch_ns_for({ end, aDateTime.time }, aTimeZone);
            w.startDuration = { startDate, i128{} };
            w.endDuration = { endDate, i128{} };
            return w;
        }

        nudge_result nudge_to_calendar_unit(int aSign, internal_duration const& aDuration, i128 const& aOriginNs, i128 const& aDestNs,
            iso_date_time const& aDateTime, calendar_id aCalendar, time_zone const* aTimeZone, std::int64_t aIncrement, unit aUnit,
            rounding_mode aMode, double* aTotal)
        {
            bool didExpand = false;
            auto w = compute_nudge_window(aSign, aDuration, aOriginNs, aDateTime, aCalendar, aTimeZone, aIncrement, aUnit, false);
            bool const inside = aSign == 1 ? (w.startEpochNs <= aDestNs && aDestNs <= w.endEpochNs) :
                (w.endEpochNs <= aDestNs && aDestNs <= w.startEpochNs);
            if (!inside)
            {
                w = compute_nudge_window(aSign, aDuration, aOriginNs, aDateTime, aCalendar, aTimeZone, aIncrement, aUnit, true);
                didExpand = true;
            }
            // (progress = (dest - start) / (end - start); total = r1 + progress × increment × sign)
            auto const numerator = aDestNs - w.startEpochNs;
            auto const denominator = w.endEpochNs - w.startEpochNs;
            if (aTotal != nullptr)
            {
                auto const exact = i128::from_double(w.r1) * denominator + numerator * i128{ aIncrement } * i128{ aSign };
                *aTotal = ratio_to_double(exact, denominator);
            }
            bool roundedToR2 = false;
            if (numerator == denominator)
                roundedToR2 = true;
            else
            {
                // (compare progress with 1/2; the magnitudes as the unsigned rounding mode sees them)
                auto const half = compare((numerator + numerator).abs(), denominator.abs());
                auto const r1Index = i128::from_double(std::fabs(w.r1)) / i128{ aIncrement };
                bool const r1Even = (r1Index.lo() & 1u) == 0u;
                if (numerator.zero())
                    roundedToR2 = false;
                else
                    roundedToR2 = unsigned_round(half, r1Even, aMode, aSign < 0) == 1;
            }
            nudge_result result;
            if (roundedToR2)
            {
                didExpand = true;
                result.duration = w.endDuration;
                result.nudgedEpochNs = w.endEpochNs;
            }
            else
            {
                result.duration = w.startDuration;
                result.nudgedEpochNs = w.startEpochNs;
            }
            result.didExpandCalendarUnit = didExpand;
            return result;
        }

        nudge_result nudge_to_zoned_time(int aSign, internal_duration const& aDuration, iso_date_time const& aDateTime, calendar_id aCalendar,
            time_zone const& aTimeZone, std::int64_t aIncrement, unit aUnit, rounding_mode aMode)
        {
            auto const start = calendar_date_add(aCalendar, aDateTime.date, aDuration.date, overflow::Constrain);
            auto const endDate = add_days_to_iso_date(start, aSign);
            auto const startEpochNs = get_epoch_ns_for(aTimeZone, { start, aDateTime.time }, disambiguation::Compatible);
            auto const endEpochNs = get_epoch_ns_for(aTimeZone, { endDate, aDateTime.time }, disambiguation::Compatible);
            auto const daySpan = endEpochNs - startEpochNs;
            auto const increment = unit_length(aUnit) * i128{ aIncrement };
            auto roundedTime = round_to_increment(aDuration.time, increment, aMode);
            if (roundedTime.abs() > max_time_duration())
                throw_range_error("duration is out of range");
            auto const beyondDaySpan = roundedTime - daySpan;
            bool didRoundBeyondDay = false;
            int dayDelta = 0;
            i128 nudgedEpochNs;
            if (beyondDaySpan.sign() != -aSign)
            {
                didRoundBeyondDay = true;
                dayDelta = aSign;
                roundedTime = round_to_increment(beyondDaySpan, increment, aMode);
                if (roundedTime.abs() > max_time_duration())
                    throw_range_error("duration is out of range");
                nudgedEpochNs = endEpochNs + roundedTime;
            }
            else
                nudgedEpochNs = startEpochNs + roundedTime;
            auto const dateDuration = adjust_date_duration(aDuration.date, aDuration.date.days + dayDelta);
            return { { dateDuration, roundedTime }, nudgedEpochNs, didRoundBeyondDay };
        }

        nudge_result nudge_to_day_or_time(internal_duration const& aDuration, i128 const& aDestNs, unit aLargestUnit, std::int64_t aIncrement,
            unit aSmallestUnit, rounding_mode aMode)
        {
            auto const timeDuration = aDuration.time + i128::from_double(aDuration.date.days) * i128{ nsPerDay };
            auto const roundedTime = round_to_increment(timeDuration, unit_length(aSmallestUnit) * i128{ aIncrement }, aMode);
            if (roundedTime.abs() > max_time_duration())
                throw_range_error("duration is out of range");
            auto const diffTime = roundedTime - timeDuration;
            auto const wholeDays = timeDuration / i128{ nsPerDay };
            auto const roundedWholeDays = roundedTime / i128{ nsPerDay };
            auto const dayDelta = roundedWholeDays - wholeDays;
            bool const didExpandDays = dayDelta.sign() == timeDuration.sign();
            auto const nudgedEpochNs = aDestNs + diffTime;
            double days = 0.0;
            auto remainder = roundedTime;
            if (is_date_unit(aLargestUnit))
            {
                days = roundedWholeDays.to_double();
                remainder = roundedTime - roundedWholeDays * i128{ nsPerDay };
            }
            auto const dateDuration = adjust_date_duration(aDuration.date, days);
            return { { dateDuration, remainder }, nudgedEpochNs, didExpandDays };
        }

        internal_duration bubble_relative_duration(int aSign, internal_duration aDuration, i128 const& aNudgedEpochNs, iso_date_time const& aDateTime,
            calendar_id aCalendar, time_zone const* aTimeZone, unit aLargestUnit, unit aSmallestUnit)
        {
            if (aSmallestUnit == aLargestUnit)
                return aDuration;
            auto const largestIndex = static_cast<int>(aLargestUnit);
            for (int unitIndex = static_cast<int>(aSmallestUnit) - 1; unitIndex >= largestIndex; --unitIndex)
            {
                auto const u = static_cast<unit>(unitIndex);
                if (u == unit::Week && aLargestUnit != unit::Week)
                    continue;
                date_duration endDuration;
                if (u == unit::Year)
                    endDuration = create_date_duration(aDuration.date.years + aSign, 0.0, 0.0, 0.0);
                else if (u == unit::Month)
                    endDuration = adjust_date_duration(aDuration.date, 0.0, 0.0, aDuration.date.months + aSign);
                else
                    endDuration = adjust_date_duration(aDuration.date, 0.0, aDuration.date.weeks + aSign);
                auto const end = calendar_date_add(aCalendar, aDateTime.date, endDuration, overflow::Constrain);
                auto const endEpochNs = epoch_ns_for({ end, aDateTime.time }, aTimeZone);
                auto const beyondEnd = aNudgedEpochNs - endEpochNs;
                if (beyondEnd.sign() != -aSign)
                    aDuration = { endDuration, i128{} };
                else
                    break;
            }
            return aDuration;
        }
    }

    internal_duration round_relative_duration(internal_duration aDuration, i128 const& aOriginNs, i128 const& aDestNs,
        iso_date_time const& aDateTime, calendar_id aCalendar, time_zone const* aTimeZone, unit aLargestUnit, std::int64_t aIncrement,
        unit aSmallestUnit, rounding_mode aMode)
    {
        bool const irregularLengthUnit = is_calendar_unit(aSmallestUnit) || (aTimeZone != nullptr && aSmallestUnit == unit::Day);
        int const sign = internal_duration_sign(aDuration) < 0 ? -1 : 1;
        nudge_result nudge;
        if (irregularLengthUnit)
            nudge = nudge_to_calendar_unit(sign, aDuration, aOriginNs, aDestNs, aDateTime, aCalendar, aTimeZone, aIncrement, aSmallestUnit, aMode,
                nullptr);
        else if (aTimeZone != nullptr)
            nudge = nudge_to_zoned_time(sign, aDuration, aDateTime, aCalendar, *aTimeZone, aIncrement, aSmallestUnit, aMode);
        else
            nudge = nudge_to_day_or_time(aDuration, aDestNs, aLargestUnit, aIncrement, aSmallestUnit, aMode);
        aDuration = nudge.duration;
        if (nudge.didExpandCalendarUnit && aSmallestUnit != unit::Week)
        {
            auto const startUnit = larger_unit(aSmallestUnit, unit::Day);
            aDuration = bubble_relative_duration(sign, aDuration, nudge.nudgedEpochNs, aDateTime, aCalendar, aTimeZone, aLargestUnit, startUnit);
        }
        return aDuration;
    }

    double total_relative_duration(internal_duration const& aDuration, i128 const& aOriginNs, i128 const& aDestNs,
        iso_date_time const& aDateTime, calendar_id aCalendar, time_zone const* aTimeZone, unit aUnit)
    {
        if (is_calendar_unit(aUnit) || (aTimeZone != nullptr && aUnit == unit::Day))
        {
            int const sign = internal_duration_sign(aDuration) < 0 ? -1 : 1;
            double total = 0.0;
            nudge_to_calendar_unit(sign, aDuration, aOriginNs, aDestNs, aDateTime, aCalendar, aTimeZone, 1, aUnit, rounding_mode::Trunc, &total);
            return total;
        }
        auto const timeDuration = add_24_hour_days(aDuration.time, aDuration.date.days);
        return total_time_duration(timeDuration, aUnit);
    }

    internal_duration difference_iso_date_time(calendar_id aCalendar, iso_date_time const& a, iso_date_time const& b, unit aLargestUnit)
    {
        auto timeDuration = difference_time(a.time, b.time);
        auto const timeSign = timeDuration.sign();
        auto const dateSign = compare_iso_date(a.date, b.date);
        auto adjustedDate = b.date;
        if (timeSign == dateSign)
        {
            adjustedDate = add_days_to_iso_date(adjustedDate, timeSign);
            timeDuration = add_24_hour_days(timeDuration, -timeSign);
        }
        auto const dateLargestUnit = larger_unit(unit::Day, aLargestUnit);
        auto dateDifference = calendar_date_until(aCalendar, a.date, adjustedDate, dateLargestUnit);
        if (aLargestUnit != dateLargestUnit)
        {
            timeDuration = add_24_hour_days(timeDuration, dateDifference.days);
            dateDifference.days = 0.0;
        }
        return { dateDifference, timeDuration };
    }

    internal_duration difference_plain_date_time_with_rounding(calendar_id aCalendar, iso_date_time const& a, iso_date_time const& b,
        unit aLargestUnit, std::int64_t aIncrement, unit aSmallestUnit, rounding_mode aMode)
    {
        if (compare_iso_date_time(a, b) == 0)
            return {};
        if (!iso_date_time_within_limits(a) || !iso_date_time_within_limits(b))
            throw_range_error("date-time is outside the representable range");
        auto const diff = difference_iso_date_time(aCalendar, a, b, aLargestUnit);
        if (aSmallestUnit == unit::Nanosecond && aIncrement == 1)
            return diff;
        return round_relative_duration(diff, get_utc_epoch_ns(a), get_utc_epoch_ns(b), a, aCalendar, nullptr, aLargestUnit, aIncrement,
            aSmallestUnit, aMode);
    }

    double difference_plain_date_time_with_total(calendar_id aCalendar, iso_date_time const& a, iso_date_time const& b, unit aUnit)
    {
        if (compare_iso_date_time(a, b) == 0)
            return 0.0;
        if (!iso_date_time_within_limits(a) || !iso_date_time_within_limits(b))
            throw_range_error("date-time is outside the representable range");
        auto const diff = difference_iso_date_time(aCalendar, a, b, aUnit);
        if (aUnit == unit::Nanosecond)
            return diff.time.to_double();
        return total_relative_duration(diff, get_utc_epoch_ns(a), get_utc_epoch_ns(b), a, aCalendar, nullptr, aUnit);
    }

    internal_duration difference_instant(i128 const& a, i128 const& b, std::int64_t aIncrement, unit aSmallestUnit, rounding_mode aMode)
    {
        return { {}, round_time_duration(b - a, aIncrement, aSmallestUnit, aMode) };
    }

    i128 add_instant(i128 const& aEpochNs, i128 const& aTime)
    {
        auto const result = aEpochNs + aTime;
        if (!is_valid_epoch_ns(result))
            throw_range_error("instant is outside the representable range");
        return result;
    }

    i128 add_zoned_date_time(i128 const& aEpochNs, time_zone const& aTimeZone, calendar_id aCalendar, internal_duration const& aDuration,
        overflow aOverflow)
    {
        if (date_duration_sign(aDuration.date) == 0)
            return add_instant(aEpochNs, aDuration.time);
        auto const dateTime = get_iso_date_time_for(aTimeZone, aEpochNs);
        auto const addedDate = calendar_date_add(aCalendar, dateTime.date, aDuration.date, aOverflow);
        iso_date_time const intermediate{ addedDate, dateTime.time };
        if (!iso_date_time_within_limits(intermediate))
            throw_range_error("date-time is outside the representable range");
        auto const intermediateNs = get_epoch_ns_for(aTimeZone, intermediate, disambiguation::Compatible);
        return add_instant(intermediateNs, aDuration.time);
    }

    internal_duration difference_zoned_date_time(i128 const& a, i128 const& b, time_zone const& aTimeZone, calendar_id aCalendar,
        unit aLargestUnit)
    {
        if (a == b)
            return {};
        auto const start = get_iso_date_time_for(aTimeZone, a);
        auto const end = get_iso_date_time_for(aTimeZone, b);
        if (compare_iso_date(start.date, end.date) == 0)
            return { {}, b - a };
        int const sign = (b - a).negative() ? 1 : -1;
        int const maxDayCorrection = sign == -1 ? 2 : 1;
        int dayCorrection = 0;
        auto timeDuration = difference_time(start.time, end.time);
        if (timeDuration.sign() == sign)
            ++dayCorrection;
        bool success = false;
        iso_date_time intermediate;
        while (dayCorrection <= maxDayCorrection && !success)
        {
            auto const intermediateDate = add_days_to_iso_date(end.date, dayCorrection * sign);
            intermediate = { intermediateDate, start.time };
            auto const intermediateNs = get_epoch_ns_for(aTimeZone, intermediate, disambiguation::Compatible);
            timeDuration = b - intermediateNs;
            if (sign != timeDuration.sign())
                success = true;
            ++dayCorrection;
        }
        auto const dateLargestUnit = larger_unit(aLargestUnit, unit::Day);
        auto const dateDifference = calendar_date_until(aCalendar, start.date, intermediate.date, dateLargestUnit);
        return { dateDifference, timeDuration };
    }

    internal_duration difference_zoned_date_time_with_rounding(i128 const& a, i128 const& b, time_zone const& aTimeZone, calendar_id aCalendar,
        unit aLargestUnit, std::int64_t aIncrement, unit aSmallestUnit, rounding_mode aMode)
    {
        if (!is_date_unit(aLargestUnit))
            return difference_instant(a, b, aIncrement, aSmallestUnit, aMode);
        auto const difference = difference_zoned_date_time(a, b, aTimeZone, aCalendar, aLargestUnit);
        if (aSmallestUnit == unit::Nanosecond && aIncrement == 1)
            return difference;
        auto const dateTime = get_iso_date_time_for(aTimeZone, a);
        return round_relative_duration(difference, a, b, dateTime, aCalendar, &aTimeZone, aLargestUnit, aIncrement, aSmallestUnit, aMode);
    }

    double difference_zoned_date_time_with_total(i128 const& a, i128 const& b, time_zone const& aTimeZone, calendar_id aCalendar, unit aUnit)
    {
        if (!is_date_unit(aUnit))
            return total_time_duration(b - a, aUnit);
        auto const difference = difference_zoned_date_time(a, b, aTimeZone, aCalendar, aUnit);
        auto const dateTime = get_iso_date_time_for(aTimeZone, a);
        return total_relative_duration(difference, a, b, dateTime, aCalendar, &aTimeZone, aUnit);
    }

    i128 interpret_iso_date_time_offset(iso_date const& aDate, std::optional<time_record> const& aTime, int aOffsetBehaviour,
        std::int64_t aOffsetNs, time_zone const& aTimeZone, disambiguation aDisambiguation, offset_option aOffsetOption, bool aMatchMinutes)
    {
        // (aOffsetBehaviour: 0 option, 1 exact, 2 wall)
        if (!aTime)
            return get_start_of_day(aTimeZone, aDate);
        iso_date_time const dateTime{ aDate, *aTime };
        if (aOffsetBehaviour == 2 || (aOffsetBehaviour == 0 && aOffsetOption == offset_option::Ignore))
            return get_epoch_ns_for(aTimeZone, dateTime, aDisambiguation);
        if (aOffsetBehaviour == 1 || (aOffsetBehaviour == 0 && aOffsetOption == offset_option::Use))
        {
            auto const& t = *aTime;
            auto const balanced = balance_iso_date_time(aDate, t.hour, t.minute, t.second, t.millisecond, t.microsecond,
                i128{ t.nanosecond } - i128{ aOffsetNs });
            check_iso_days_range(balanced.date);
            auto const epochNs = get_utc_epoch_ns(balanced);
            if (!is_valid_epoch_ns(epochNs))
                throw_range_error("date-time is outside the representable range");
            return epochNs;
        }
        check_iso_days_range(aDate);
        auto const utcEpochNs = get_utc_epoch_ns(dateTime);
        auto const possible = get_possible_epoch_ns(aTimeZone, dateTime);
        for (auto const& candidate : possible)
        {
            auto const candidateOffset = utcEpochNs - candidate;
            if (candidateOffset == i128{ aOffsetNs })
                return candidate;
            if (aMatchMinutes)
            {
                auto const rounded = round_to_increment(candidateOffset, i128{ 60000000000LL }, rounding_mode::HalfExpand);
                if (rounded == i128{ aOffsetNs })
                    return candidate;
            }
        }
        if (aOffsetOption == offset_option::Reject)
            throw_range_error("the offset does not agree with the time zone");
        return disambiguate_possible_epoch_ns(possible, aTimeZone, dateTime, aDisambiguation);
    }
}
