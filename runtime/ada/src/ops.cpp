/*
  ops.cpp

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

// The Ada runtime: predefined operations that generated code calls rather than expands: exponentiation, the attributes of
// floating point types, concatenation and the logical operators of Boolean arrays, tag membership, and a few values

#include <cmath>
#include <algorithm>
#include <cstring>
#include <limits>
#include <string>
#include <map>
#include <vector>
#include "core.hpp"
#include "exec.hpp"
#include "int128.hpp"

using namespace neos::ada_runtime;

namespace neos::ada_runtime
{
    // (access types' collections of controlled objects (any task's allocators add to them), by id)
    std::map<std::int64_t, std::vector<execution_state::finalizable>>& object_collections()
    {
        static std::map<std::int64_t, std::vector<execution_state::finalizable>> sCollections;
        return sCollections;
    }

    // (objects allocated from subpools (13.11.4) that need finalization: their subpools (handles), by address)
    std::map<std::int64_t, std::int64_t>& subpool_objects()
    {
        static std::map<std::int64_t, std::int64_t> sObjects;
        return sObjects;
    }

    std::vector<execution_state::finalizable> collection_objects(std::int64_t aCollection)
    {
        auto& collections = object_collections();
        auto existing = collections.find(aCollection);
        if (existing == collections.end())
            return {};
        auto result = std::move(existing->second);
        collections.erase(existing);
        for (auto const& o : result)
            subpool_objects().erase(o.object);
        return result;
    }
}

namespace
{
    // --- exponentiation

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_power_integer(std::int64_t aBase, std::int64_t aExponent)
    {
        std::int64_t result = 1;
        std::int64_t base = aBase;
        for (std::int64_t e = aExponent; e > 0; e >>= 1)
        {
            if (e & 1)
            {
                if (mul_overflow(result, base, &result))
                {
                    raise_predefined(0, "overflow check failed");
                    return 0;
                }
            }
            if (e > 1 && mul_overflow(base, base, &base))
            {
                // (overflow only matters if this square is used: 0 ** n, 1 ** n, -1 ** n never get here)
                raise_predefined(0, "overflow check failed");
                return 0;
            }
        }
        return result;
    }

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_power_modular(std::int64_t aBase, std::int64_t aExponent, std::int64_t aModulus)
    {
        auto const m = static_cast<uint128>(static_cast<std::uint64_t>(aModulus));
        auto const reduce = [&](uint128 v) { return aModulus == 0 ? static_cast<uint128>(static_cast<std::uint64_t>(v)) : v % m; };
        uint128 result = reduce(1u);
        uint128 base = reduce(static_cast<std::uint64_t>(aBase));
        for (std::int64_t e = aExponent; e > 0; e >>= 1)
        {
            if (e & 1)
                result = reduce(result * base);
            base = reduce(base * base);
        }
        return static_cast<std::int64_t>(static_cast<std::uint64_t>(result));
    }

    NEOS_ADA_PRIMITIVE double neos_ada_power_real(double aBase, std::int64_t aExponent)
    {
        if (aBase == 0.0 && aExponent < 0)
        {
            raise_predefined(0, "division by zero");
            return 0.0;
        }
        // (repeated multiplication (as the RM's accuracy model), then the reciprocal of a negative exponent's)
        double result = 1.0;
        double base = aBase;
        for (std::int64_t e = aExponent < 0 ? -aExponent : aExponent; e > 0; e >>= 1)
        {
            if (e & 1)
                result *= base;
            base *= base;
        }
        return aExponent < 0 ? 1.0 / result : result;
    }

    // (X'Mod: the integer's value modulo the modulus)
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_mod_integer(std::int64_t aValue, std::int64_t aModulus)
    {
        if (aModulus == 0)
            return aValue;
        auto const m = static_cast<std::uint64_t>(aModulus);
        if (aValue >= 0)
            return static_cast<std::int64_t>(static_cast<std::uint64_t>(aValue) % m);
        auto const r = (static_cast<std::uint64_t>(-(aValue + 1)) % m);
        return static_cast<std::int64_t>(m - 1u - r);
    }

    // --- floating point attributes (A.5.3)

    NEOS_ADA_PRIMITIVE double neos_ada_float_succ(double aValue, std::int32_t aSingle)
    {
        if (aSingle)
            return static_cast<double>(std::nextafter(static_cast<float>(aValue), std::numeric_limits<float>::infinity()));
        return std::nextafter(aValue, std::numeric_limits<double>::infinity());
    }

    NEOS_ADA_PRIMITIVE double neos_ada_float_pred(double aValue, std::int32_t aSingle)
    {
        if (aSingle)
            return static_cast<double>(std::nextafter(static_cast<float>(aValue), -std::numeric_limits<float>::infinity()));
        return std::nextafter(aValue, -std::numeric_limits<double>::infinity());
    }

    // (Ada's exponent and fraction: X = Fraction * 2 ** Exponent, 0.5 <= |Fraction| < 1 (frexp's))
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_float_exponent(double aValue)
    {
        int e = 0;
        if (aValue == 0.0)
            return 0;
        std::frexp(aValue, &e);
        return e;
    }

    NEOS_ADA_PRIMITIVE double neos_ada_float_fraction(double aValue)
    {
        int e = 0;
        return std::frexp(aValue, &e);
    }

    NEOS_ADA_PRIMITIVE double neos_ada_float_scaling(double aValue, std::int64_t aAdjustment)
    {
        return std::ldexp(aValue, static_cast<int>(aAdjustment));
    }

    NEOS_ADA_PRIMITIVE double neos_ada_float_compose(double aFraction, std::int64_t aExponent)
    {
        int e = 0;
        auto const f = std::frexp(aFraction, &e);
        return std::ldexp(f, static_cast<int>(aExponent));
    }

    NEOS_ADA_PRIMITIVE double neos_ada_float_remainder(double aX, double aY)
    {
        if (aY == 0.0)
        {
            raise_predefined(0, "division by zero");
            return 0.0;
        }
        return std::remainder(aX, aY);
    }

    NEOS_ADA_PRIMITIVE double neos_ada_float_adjacent(double aX, double aTowards)
    {
        return std::nextafter(aX, aTowards);
    }

    NEOS_ADA_PRIMITIVE double neos_ada_float_leading_part(double aX, std::int64_t aRadixDigits)
    {
        if (aRadixDigits <= 0)
        {
            raise_predefined(0, "range check failed");
            return 0.0;
        }
        if (aX == 0.0)
            return 0.0;
        int e = 0;
        std::frexp(aX, &e);
        auto const scale = std::ldexp(1.0, static_cast<int>(aRadixDigits) - e);
        return std::trunc(aX * scale) / scale;
    }

    // --- arrays (descriptors: data offset (relative to the descriptor), then first and last per dimension)

    std::int64_t first_of(address aDescriptor) { return *at<std::int64_t>(aDescriptor + 8); }
    std::int64_t last_of(address aDescriptor) { return *at<std::int64_t>(aDescriptor + 16); }
    std::int64_t length_of(address aDescriptor) { auto const f = first_of(aDescriptor); auto const l = last_of(aDescriptor); return l < f ? 0 : l - f + 1; }
    address data_of(address aDescriptor) { return aDescriptor + *at<std::int64_t>(aDescriptor); }

    address new_array(std::int64_t aFirst, std::int64_t aLength, std::int64_t aElementSize)
    {
        auto const descriptor = ss_allocate(24u + static_cast<std::uint64_t>(aLength * aElementSize));
        *at<std::int64_t>(descriptor) = 24;
        *at<std::int64_t>(descriptor + 8) = aFirst;
        *at<std::int64_t>(descriptor + 16) = aFirst + aLength - 1;
        return descriptor;
    }

    // (bit-packed arrays (a negative element size: their components' bits, negated): their descriptors' data offset is in bits (from
    // the descriptor's address times 8), a component at a bit address)
    std::int64_t bit_data_of(address aDescriptor) { return static_cast<std::int64_t>(aDescriptor) * 8 + *at<std::int64_t>(aDescriptor); }
    std::uint64_t bits_at(std::int64_t aBitAddress, std::int64_t aBits)
    {
        return (static_cast<std::uint64_t>(*at<std::uint8_t>(static_cast<address>(aBitAddress >> 3))) >> (aBitAddress & 7)) & ((std::uint64_t{ 1 } << aBits) - 1u);
    }
    void set_bits(std::int64_t aBitAddress, std::int64_t aBits, std::uint64_t aValue)
    {
        auto& unit = *at<std::uint8_t>(static_cast<address>(aBitAddress >> 3));
        auto const mask = ((1u << aBits) - 1u) << (aBitAddress & 7);
        unit = static_cast<std::uint8_t>((unit & ~mask) | ((static_cast<unsigned>(aValue) << (aBitAddress & 7)) & mask));
    }
    address new_bit_array(std::int64_t aFirst, std::int64_t aLength, std::int64_t aBits)
    {
        auto const descriptor = ss_allocate(24u + static_cast<std::uint64_t>((aLength * aBits + 7) / 8));
        *at<std::int64_t>(descriptor) = 24 * 8;
        *at<std::int64_t>(descriptor + 8) = aFirst;
        *at<std::int64_t>(descriptor + 16) = aFirst + aLength - 1;
        return descriptor;
    }
    void copy_bits(std::int64_t aTarget, std::int64_t aSource, std::int64_t aCount)
    {
        for (std::int64_t i = 0; i < aCount; ++i)
            set_bits(aTarget + i, 1, bits_at(aSource + i, 1));
    }

    // (Left & Right: a new array (on the secondary stack); a null left operand: the right operand's bounds)
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_concatenate(std::int64_t aElementSize, std::int64_t aLeft, std::int64_t aRight, std::int64_t aIndexFirst)
    {
        (void)aIndexFirst;
        auto const ln = length_of(aLeft);
        auto const rn = length_of(aRight);
        if (aElementSize < 0)
        {
            // (bit-packed)
            auto const bits = -aElementSize;
            std::int64_t first = ln == 0 ? first_of(aRight) : first_of(aLeft);
            std::int64_t last = 0;
            if (ln != 0 && add_overflow(first, ln + rn - 1, &last))
            {
                raise_predefined(0, "range check failed");
                return 0;
            }
            auto const result = new_bit_array(first, ln + rn, bits);
            if (ln == 0)
                *at<std::int64_t>(result + 16) = last_of(aRight);
            copy_bits(bit_data_of(result), bit_data_of(aLeft), ln * bits);
            copy_bits(bit_data_of(result) + ln * bits, bit_data_of(aRight), rn * bits);
            return result;
        }
        if (ln == 0)
        {
            auto const result = new_array(first_of(aRight), rn, aElementSize);
            *at<std::int64_t>(result + 16) = last_of(aRight);
            std::memcpy(chars(data_of(result)), chars(data_of(aRight)), static_cast<std::size_t>(rn * aElementSize));
            return result;
        }
        auto const first = first_of(aLeft);
        std::int64_t last = 0;
        if (add_overflow(first, ln + rn - 1, &last))
        {
            raise_predefined(0, "range check failed");
            return 0;
        }
        auto const result = new_array(first, ln + rn, aElementSize);
        std::memcpy(chars(data_of(result)), chars(data_of(aLeft)), static_cast<std::size_t>(ln * aElementSize));
        std::memcpy(chars(data_of(result) + ln * aElementSize), chars(data_of(aRight)), static_cast<std::size_t>(rn * aElementSize));
        return result;
    }

    std::uint64_t element_at(address aData, std::int64_t aIndex, std::int64_t aSize)
    {
        std::uint64_t v = 0u;
        std::memcpy(&v, chars(aData + aIndex * aSize), static_cast<std::size_t>(aSize));
        return v;
    }

    void set_element(address aData, std::int64_t aIndex, std::int64_t aSize, std::uint64_t aValue)
    {
        std::memcpy(chars(aData + aIndex * aSize), &aValue, static_cast<std::size_t>(aSize));
    }

    // (and, or, xor of two Boolean arrays of the same length: the left's bounds)
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_array_logical(std::int64_t aLeft, std::int64_t aRight, std::int64_t aElementSize, std::int32_t aOp)
    {
        auto const n = length_of(aLeft);
        if (n != length_of(aRight))
        {
            raise_predefined(0, "length check failed");
            return 0;
        }
        if (aElementSize < 0)
        {
            // (bit-packed: their components' bits)
            auto const bits = -aElementSize;
            auto const result = new_bit_array(first_of(aLeft), n, bits);
            *at<std::int64_t>(result + 16) = last_of(aLeft);
            auto const l = bit_data_of(aLeft), r = bit_data_of(aRight), d = bit_data_of(result);
            for (std::int64_t i = 0; i < n; ++i)
            {
                auto const a = bits_at(l + i * bits, bits) != 0u, b = bits_at(r + i * bits, bits) != 0u;
                set_bits(d + i * bits, bits, aOp == 0 ? (a && b) : aOp == 1 ? (a || b) : (a != b));
            }
            return result;
        }
        auto const result = new_array(first_of(aLeft), n, aElementSize);
        *at<std::int64_t>(result + 16) = last_of(aLeft);
        auto const l = data_of(aLeft), r = data_of(aRight), d = data_of(result);
        for (std::int64_t i = 0; i < n; ++i)
        {
            auto const a = element_at(l, i, aElementSize) != 0u, b = element_at(r, i, aElementSize) != 0u;
            set_element(d, i, aElementSize, aOp == 0 ? (a && b) : aOp == 1 ? (a || b) : (a != b));
        }
        return result;
    }

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_array_not(std::int64_t aArray, std::int64_t aElementSize)
    {
        auto const n = length_of(aArray);
        if (aElementSize < 0)
        {
            // (bit-packed)
            auto const bits = -aElementSize;
            auto const result = new_bit_array(first_of(aArray), n, bits);
            *at<std::int64_t>(result + 16) = last_of(aArray);
            auto const s = bit_data_of(aArray), d = bit_data_of(result);
            for (std::int64_t i = 0; i < n; ++i)
                set_bits(d + i * bits, bits, bits_at(s + i * bits, bits) == 0u ? 1u : 0u);
            return result;
        }
        auto const result = new_array(first_of(aArray), n, aElementSize);
        *at<std::int64_t>(result + 16) = last_of(aArray);
        auto const s = data_of(aArray), d = data_of(result);
        for (std::int64_t i = 0; i < n; ++i)
            set_element(d, i, aElementSize, element_at(s, i, aElementSize) == 0u ? 1u : 0u);
        return result;
    }

    // (the size of an object of a dynamically sized record type (its size descriptor: its fixed part's size, its dynamic components'
    // count, then for each its kind (0: an array: its descriptor's offset, its dimensions, its elements' size; 1: a record: the offset
    // of its offset in the object, its type's descriptor)): its fixed part's and its tail's)
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_record_size_dynamic(std::int64_t aObject, std::int64_t aDescriptor)
    {
        auto const round = [](std::int64_t aSize) { return (aSize + 7) & ~std::int64_t{ 7 }; };
        std::int64_t size = *at<std::int64_t>(aDescriptor);
        auto const count = *at<std::int64_t>(aDescriptor + 8);
        for (std::int64_t i = 0; i < count; ++i)
        {
            auto const entry = aDescriptor + 16 + 32 * i;
            auto const kind = *at<std::int64_t>(entry);
            auto const offset = *at<std::int64_t>(entry + 8);
            if (kind == 1)
            {
                auto const component = aObject + *at<std::int64_t>(aObject + offset);
                size += round(neos_ada_record_size_dynamic(component, *at<std::int64_t>(entry + 24)));
                continue;
            }
            auto const descriptor = aObject + offset;
            auto const dimensions = *at<std::int64_t>(entry + 16);
            std::int64_t length = 1;
            for (std::int64_t d = 0; d < dimensions; ++d)
            {
                auto const n = *at<std::int64_t>(descriptor + 16 + 16 * d) - *at<std::int64_t>(descriptor + 8 + 16 * d) + 1;
                length *= n > 0 ? n : 0;
            }
            auto const element = *at<std::int64_t>(entry + 24);
            size += round(element < 0 ? (length * -element + 7) / 8 : length * element);     // (bit-packed: its bits, negated)
        }
        return size;
    }

    // --- fixed point scaling (exact: 128-bit intermediate products)

    // (aNumerator / aDenominator (non zero): rounded to the nearest (half away from zero), or truncated (aTruncate); out of the 64-bit
    // range: Constraint_Error)
    std::int64_t scaled_quotient(int128 aNumerator, int128 aDenominator, bool aTruncate)
    {
        if (aDenominator < 0)
        {
            aNumerator = -aNumerator;
            aDenominator = -aDenominator;
        }
        int128 q = aNumerator / aDenominator;
        int128 const r = aNumerator % aDenominator;
        if (!aTruncate)
        {
            int128 const twice = (r < 0 ? -r : r) * 2;
            if (twice >= aDenominator)
                q += aNumerator < 0 ? -1 : 1;
        }
        if (q > std::numeric_limits<std::int64_t>::max() || q < std::numeric_limits<std::int64_t>::min())
        {
            raise_predefined(0, "overflow check failed");
            return 0;
        }
        return static_cast<std::int64_t>(q);
    }

    // (a fixed point (or integer) value's count times a ratio of smalls)
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_fixed_scale(std::int64_t aValue, std::int64_t aNumerator, std::int64_t aDenominator, std::int32_t aTruncate)
    {
        return scaled_quotient(static_cast<int128>(aValue) * aNumerator, aDenominator, aTruncate != 0);
    }

    // (a product (aDivide == 0) or quotient of two counts, times a ratio of smalls)
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_fixed_operation(std::int64_t aLeft, std::int64_t aRight, std::int64_t aNumerator, std::int64_t aDenominator,
        std::int32_t aDivide, std::int32_t aTruncate)
    {
        int128 n = 0;
        int128 d = 0;
        if (aDivide != 0)
        {
            if (aRight == 0)
            {
                raise_predefined(0, "divide by zero");
                return 0;
            }
            if (mul_overflow(static_cast<int128>(aLeft), static_cast<int128>(aNumerator), &n) ||
                mul_overflow(static_cast<int128>(aRight), static_cast<int128>(aDenominator), &d))
            {
                raise_predefined(0, "overflow check failed");
                return 0;
            }
        }
        else if (mul_overflow(static_cast<int128>(aLeft) * aRight, static_cast<int128>(aNumerator), &n))
        {
            raise_predefined(0, "overflow check failed");
            return 0;
        }
        else
            d = aDenominator;
        return scaled_quotient(n, d, aTruncate != 0);
    }

    // --- tags (a tag: its parent's tag, its expanded name, then its dispatch slots)

    // (a tag's interface table's entry for an interface (0: none): its operations' table)
    std::int64_t interface_operations(std::int64_t aTag, std::int64_t aInterface)
    {
        for (std::int64_t t = aTag; t != 0; t = *at<std::int64_t>(t))
        {
            auto const table = *at<std::int64_t>(t + 32);
            if (table == 0)
                continue;
            auto const count = *at<std::int64_t>(table);
            for (std::int64_t i = 0; i < count; ++i)
                if (*at<std::int64_t>(table + 8 + 16 * i) == aInterface)
                    return *at<std::int64_t>(table + 16 + 16 * i);
        }
        return 0;
    }

    NEOS_ADA_PRIMITIVE std::int32_t neos_ada_is_descendant(std::int64_t aTag, std::int64_t aAncestor)
    {
        for (std::int64_t t = aTag; t != 0; t = *at<std::int64_t>(t))
            if (t == aAncestor)
                return 1;
        // (an interface that it implements)
        return aTag != 0 && interface_operations(aTag, aAncestor) != 0 ? 1 : 0;
    }

    // (an interface's operation (its slot) for an object (its tag): its implementation's function)
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_interface_function(std::int64_t aTag, std::int64_t aInterface, std::int64_t aSlot)
    {
        auto const operations = aTag != 0 ? interface_operations(aTag, aInterface) : 0;
        if (operations == 0)
        {
            raise_predefined(1, "the object's type does not implement the interface");
            return 0;
        }
        return *at<std::int64_t>(operations + 8 * aSlot);
    }

    // (the entry that implements an interface's operation (its slot, of the interface's slots), for a dispatching requeue: its
    // descriptor (kind: 1 a task's entry, 2 a protected object's; its index), or 0: not implemented by an entry)
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_interface_entry(std::int64_t aTag, std::int64_t aInterface, std::int64_t aSlot, std::int64_t aSlots)
    {
        auto const operations = aTag != 0 ? interface_operations(aTag, aInterface) : 0;
        if (operations == 0)
            return 0;
        auto const descriptor = operations + 8 * aSlots + 16 * aSlot;
        return *at<std::int64_t>(descriptor) != 0 ? descriptor : 0;
    }

    // (a tag's parent's tag (0: none))
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_tag_parent(std::int64_t aTag)
    {
        return aTag != 0 ? *at<std::int64_t>(aTag) : 0;
    }

    // (a tag's type's accessibility level)
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_tag_level(std::int64_t aTag)
    {
        return aTag != 0 ? *at<std::int64_t>(aTag + 40) : 0;
    }

    // (whether a tag's type is abstract (its flags' bit 0))
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_tag_abstract(std::int64_t aTag)
    {
        return aTag != 0 && (*at<std::int64_t>(aTag + 64) & 1) != 0 ? 1 : 0;
    }

    // (a tag's external tag: its specified one, else its expanded name (a static string))
    std::int64_t external_of(std::int64_t aTag)
    {
        auto const external = *at<std::int64_t>(aTag + 72);
        return external != 0 ? external : *at<std::int64_t>(aTag + 8);
    }

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_tag_external(std::int64_t aTag)
    {
        return aTag != 0 ? static_cast<std::int64_t>(new_string(string_at(external_of(aTag)))) : static_cast<std::int64_t>(new_string({}));
    }

    // (an External_Tag specification elaborated: Program_Error if another's (elaborated) is the same (13.3(76.1/3)))
    NEOS_ADA_PRIMITIVE void neos_ada_tag_register_external(std::int64_t aTag)
    {
        static std::map<std::string, std::int64_t> sExternals;
        auto const name = string_at(external_of(aTag));
        auto const existing = sExternals.find(name);
        if (existing != sExternals.end() && existing->second != aTag)
        {
            raise_predefined(1, "two types have the same external tag");
            return;
        }
        sExternals[name] = aTag;
    }

    // (a tag's expanded name: a new string)
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_tag_name(std::int64_t aTag)
    {
        return aTag != 0 ? static_cast<std::int64_t>(new_string(string_at(*at<std::int64_t>(aTag + 8)))) : static_cast<std::int64_t>(new_string({}));
    }

    // (the tag whose expanded name is a string (the program's tags: a table (its count, then the tags) whose address is in a
    // cell); 0: none)
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_tag_internal(std::int64_t aName, std::int64_t aCell)
    {
        auto name = string_at(aName);
        for (auto& c : name)
            if (c >= 'a' && c <= 'z')
                c = static_cast<char>(c - 'a' + 'A');
        auto const table = aCell != 0 ? *at<std::int64_t>(aCell) : 0;
        if (table == 0)
            return 0;
        auto const count = *at<std::int64_t>(table);
        for (std::int64_t i = 0; i < count; ++i)
        {
            auto const tag = *at<std::int64_t>(table + 8 + 8 * i);
            auto external = string_at(external_of(tag));
            for (auto& c : external)
                if (c >= 'a' && c <= 'z')
                    c = static_cast<char>(c - 'a' + 'A');
            if (external == name)
                return tag;
        }
        return 0;
    }

    // --- values

    // (Character'Value: a character literal ('x') or the name of a control character)
    // (a wide string (its elements of the size given): a String of its characters, UTF-8 encoded above 127 (for 'Wide_Value))
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_narrow_string(std::int64_t aWide, std::int64_t aElementSize)
    {
        if (aWide == 0)
            return static_cast<std::int64_t>(new_string(std::string{}));
        auto const data = static_cast<address>(aWide) + *at<std::int64_t>(static_cast<address>(aWide));
        auto const first = *at<std::int64_t>(static_cast<address>(aWide) + 8);
        auto const last = *at<std::int64_t>(static_cast<address>(aWide) + 16);
        std::string text;
        for (std::int64_t i = 0; first <= last && i <= last - first; ++i)
        {
            std::uint32_t c = 0u;
            std::memcpy(&c, memory() + data + static_cast<address>(i * aElementSize), static_cast<std::size_t>(std::min<std::int64_t>(aElementSize, 4)));
            if (c < 0x80u)
                text += static_cast<char>(c);
            else if (c < 0x800u)
            {
                text += static_cast<char>(0xC0u | (c >> 6u));
                text += static_cast<char>(0x80u | (c & 0x3Fu));
            }
            else if (c < 0x10000u)
            {
                text += static_cast<char>(0xE0u | (c >> 12u));
                text += static_cast<char>(0x80u | ((c >> 6u) & 0x3Fu));
                text += static_cast<char>(0x80u | (c & 0x3Fu));
            }
            else
            {
                text += static_cast<char>(0xF0u | (c >> 18u));
                text += static_cast<char>(0x80u | ((c >> 12u) & 0x3Fu));
                text += static_cast<char>(0x80u | ((c >> 6u) & 0x3Fu));
                text += static_cast<char>(0x80u | (c & 0x3Fu));
            }
        }
        return static_cast<std::int64_t>(new_string(text));
    }

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_value_character(std::int64_t aString)
    {
        auto text = string_at(aString);
        auto const b = text.find_first_not_of(" ");
        auto const e = text.find_last_not_of(" ");
        text = b == std::string::npos ? std::string{} : text.substr(b, e - b + 1u);
        if (text.size() == 3u && text[0] == '\'' && text[2] == '\'')
            return static_cast<unsigned char>(text[1]);
        // (a UTF-8 encoded character between quotes)
        if (text.size() >= 4u && text.front() == '\'' && text.back() == '\'')
        {
            auto const* p = reinterpret_cast<unsigned char const*>(text.data() + 1);
            std::uint32_t c = 0u;
            std::size_t n = 0u;
            if ((p[0] & 0xE0u) == 0xC0u) { c = p[0] & 0x1Fu; n = 1u; }
            else if ((p[0] & 0xF0u) == 0xE0u) { c = p[0] & 0x0Fu; n = 2u; }
            else if ((p[0] & 0xF8u) == 0xF0u) { c = p[0] & 0x07u; n = 3u; }
            if (n + 3u == text.size())
            {
                for (std::size_t i = 1u; i <= n; ++i)
                    c = (c << 6u) | (p[i] & 0x3Fu);
                return c;
            }
        }
        static char const* const sControls[] =
        {
            "NUL", "SOH", "STX", "ETX", "EOT", "ENQ", "ACK", "BEL", "BS", "HT", "LF", "VT", "FF", "CR", "SO", "SI",
            "DLE", "DC1", "DC2", "DC3", "DC4", "NAK", "SYN", "ETB", "CAN", "EM", "SUB", "ESC", "FS", "GS", "RS", "US"
        };
        for (auto& c : text)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        for (std::size_t i = 0u; i < 32u; ++i)
            if (text == sControls[i])
                return static_cast<std::int64_t>(i);
        if (text == "DEL")
            return 127;
        // (the names of the characters 128 .. 159 (A.1(7.x)), and of a wide character's code (Hex_hhhhhhhh))
        static char const* const sHigh[] =
        {
            "RESERVED_128", "RESERVED_129", "BPH", "NBH", "RESERVED_132", "NEL", "SSA", "ESA", "HTS", "HTJ", "VTS", "PLD", "PLU", "RI", "SS2",
            "SS3", "DCS", "PU1", "PU2", "STS", "CCH", "MW", "SPA", "EPA", "SOS", "RESERVED_153", "SCI", "CSI", "ST", "OSC", "PM", "APC"
        };
        for (std::size_t i = 0u; i < 32u; ++i)
            if (text == sHigh[i])
                return static_cast<std::int64_t>(128u + i);
        if (text.size() == 12u && text.compare(0u, 4u, "HEX_") == 0 && text.find_first_not_of("0123456789ABCDEF", 4u) == std::string::npos)
            return static_cast<std::int64_t>(std::stoul(text.substr(4u), nullptr, 16));
        raise_predefined(0, "bad input for 'Value");
        return 0;
    }

    // --- controlled objects (the task's finalization chain)

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_fin_mark()
    {
        return static_cast<std::int64_t>(state().finalization.size());
    }

    NEOS_ADA_PRIMITIVE void neos_ada_fin_register(std::int64_t aAddress, std::int64_t aFunction, std::int64_t aLink)
    {
        state().finalization.push_back({ aAddress, aFunction, aLink });
    }

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_fin_collection_create()
    {
        // (a collection: finalized (its objects) when the master that declares its access type is left)
        static std::int64_t sNext = 0;
        auto const id = ++sNext;
        object_collections()[id];
        state().finalization.push_back({ id, ObjectsEntry, 0 });
        return id;
    }

    NEOS_ADA_PRIMITIVE void neos_ada_fin_collection_add(std::int64_t aCollection, std::int64_t aAddress, std::int64_t aFunction, std::int64_t aLink)
    {
        auto& collections = object_collections();
        if (auto existing = collections.find(aCollection); existing != collections.end())
            existing->second.push_back({ aAddress, aFunction, aLink });
    }

    NEOS_ADA_PRIMITIVE void neos_ada_fin_collection_add_subpool(std::int64_t aCollection, std::int64_t aAddress, std::int64_t aFunction, std::int64_t aLink,
        std::int64_t aSubpool)
    {
        // (allocated from a subpool: in its access type's collection, and finalized with the subpool if that is deallocated first)
        auto& collections = object_collections();
        if (auto existing = collections.find(aCollection); existing != collections.end())
        {
            existing->second.push_back({ aAddress, aFunction, aLink });
            subpool_objects()[aAddress] = aSubpool;
        }
    }

    NEOS_ADA_PRIMITIVE void neos_ada_fin_subpool_finalize(std::int64_t aSubpool)
    {
        // (a subpool's objects (Unchecked_Deallocate_Subpool, 13.11.5(5/3)): finalized (the last allocated first), and no longer in their
        // collections)
        auto& objects = subpool_objects();
        auto& s = state();
        auto const mark = s.finalization.size();
        for (auto& [id, collection] : object_collections())
            for (auto i = collection.begin(); i != collection.end();)
                if (auto o = objects.find(i->object); o != objects.end() && o->second == aSubpool)
                {
                    s.finalization.push_back(*i);
                    objects.erase(o);
                    i = collection.erase(i);
                }
                else
                    ++i;
        finalize_to(mark);
    }

    NEOS_ADA_PRIMITIVE void neos_ada_fin_unregister(std::int64_t aAddress)
    {
        subpool_objects().erase(aAddress);
        auto& chain = state().finalization;
        for (auto i = chain.size(); i-- > 0u;)
            if (chain[i].object == aAddress && chain[i].function >= 0)
            {
                chain.erase(chain.begin() + static_cast<std::ptrdiff_t>(i));
                return;
            }
        // (an allocated object: its collection's)
        for (auto& [id, objects] : object_collections())
            for (auto i = objects.size(); i-- > 0u;)
                if (objects[i].object == aAddress)
                {
                    objects.erase(objects.begin() + static_cast<std::ptrdiff_t>(i));
                    return;
                }
    }

    NEOS_ADA_PRIMITIVE void neos_ada_fin_release(std::int64_t aMark)
    {
        finalize_to(static_cast<std::size_t>(aMark < 0 ? 0 : aMark));
    }

    constexpr auto I64 = NEOS_RT_I64;
    constexpr auto I32 = NEOS_RT_I32;
    constexpr auto F64 = NEOS_RT_F64;
    constexpr auto VOID = NEOS_RT_VOID;

    function_table const sOpsFunctions{
        { "power_integer", NEOS_RT_FUNCTION(neos_ada_power_integer), I64, 2u, { I64, I64 }, 0u },
        { "power_modular", NEOS_RT_FUNCTION(neos_ada_power_modular), I64, 3u, { I64, I64, I64 }, 0u },
        { "power_real", NEOS_RT_FUNCTION(neos_ada_power_real), F64, 2u, { F64, I64 }, 0u },
        { "mod_integer", NEOS_RT_FUNCTION(neos_ada_mod_integer), I64, 2u, { I64, I64 }, 0u },
        { "float_succ", NEOS_RT_FUNCTION(neos_ada_float_succ), F64, 2u, { F64, I32 }, 0u },
        { "float_pred", NEOS_RT_FUNCTION(neos_ada_float_pred), F64, 2u, { F64, I32 }, 0u },
        { "float_exponent", NEOS_RT_FUNCTION(neos_ada_float_exponent), I64, 1u, { F64 }, 0u },
        { "float_fraction", NEOS_RT_FUNCTION(neos_ada_float_fraction), F64, 1u, { F64 }, 0u },
        { "float_scaling", NEOS_RT_FUNCTION(neos_ada_float_scaling), F64, 2u, { F64, I64 }, 0u },
        { "float_compose", NEOS_RT_FUNCTION(neos_ada_float_compose), F64, 2u, { F64, I64 }, 0u },
        { "float_remainder", NEOS_RT_FUNCTION(neos_ada_float_remainder), F64, 2u, { F64, F64 }, 0u },
        { "float_adjacent", NEOS_RT_FUNCTION(neos_ada_float_adjacent), F64, 2u, { F64, F64 }, 0u },
        { "float_leading_part", NEOS_RT_FUNCTION(neos_ada_float_leading_part), F64, 2u, { F64, I64 }, 0u },
        { "concatenate", NEOS_RT_FUNCTION(neos_ada_concatenate), I64, 4u, { I64, I64, I64, I64 }, 0u },
        { "array_logical", NEOS_RT_FUNCTION(neos_ada_array_logical), I64, 4u, { I64, I64, I64, I32 }, 0u },
        { "array_not", NEOS_RT_FUNCTION(neos_ada_array_not), I64, 2u, { I64, I64 }, 0u },
        { "fixed_scale", NEOS_RT_FUNCTION(neos_ada_fixed_scale), I64, 4u, { I64, I64, I64, I32 }, 0u },
        { "fixed_operation", NEOS_RT_FUNCTION(neos_ada_fixed_operation), I64, 6u, { I64, I64, I64, I64, I32, I32 }, 0u },
        { "record_size_dynamic", NEOS_RT_FUNCTION(neos_ada_record_size_dynamic), I64, 2u, { I64, I64 }, 0u },
        { "is_descendant", NEOS_RT_FUNCTION(neos_ada_is_descendant), I32, 2u, { I64, I64 }, 0u },
        { "tag_parent", NEOS_RT_FUNCTION(neos_ada_tag_parent), I64, 1u, { I64 }, 0u },
        { "interface_function", NEOS_RT_FUNCTION(neos_ada_interface_function), I64, 3u, { I64, I64, I64 }, 0u },
        { "interface_entry", NEOS_RT_FUNCTION(neos_ada_interface_entry), I64, 4u, { I64, I64, I64, I64 }, 0u },
        { "tag_name", NEOS_RT_FUNCTION(neos_ada_tag_name), I64, 1u, { I64 }, 0u },
        { "tag_external", NEOS_RT_FUNCTION(neos_ada_tag_external), I64, 1u, { I64 }, 0u },
        { "tag_register_external", NEOS_RT_FUNCTION(neos_ada_tag_register_external), VOID, 1u, { I64 }, 0u },
        { "tag_abstract", NEOS_RT_FUNCTION(neos_ada_tag_abstract), I64, 1u, { I64 }, 0u },
        { "tag_level", NEOS_RT_FUNCTION(neos_ada_tag_level), I64, 1u, { I64 }, 0u },
        { "tag_internal", NEOS_RT_FUNCTION(neos_ada_tag_internal), I64, 2u, { I64, I64 }, 0u },
        { "value_character", NEOS_RT_FUNCTION(neos_ada_value_character), I64, 1u, { I64 }, 0u },
        { "narrow_string", NEOS_RT_FUNCTION(neos_ada_narrow_string), I64, 2u, { I64, I64 }, 0u },
        { "fin_mark", NEOS_RT_FUNCTION(neos_ada_fin_mark), I64, 0u, {}, 0u },
        { "fin_register", NEOS_RT_FUNCTION(neos_ada_fin_register), VOID, 3u, { I64, I64, I64 }, 0u },
        { "fin_collection_create", NEOS_RT_FUNCTION(neos_ada_fin_collection_create), I64, 0u, {}, 0u },
        { "fin_collection_add", NEOS_RT_FUNCTION(neos_ada_fin_collection_add), VOID, 4u, { I64, I64, I64, I64 }, 0u },
        { "fin_collection_add_subpool", NEOS_RT_FUNCTION(neos_ada_fin_collection_add_subpool), VOID, 5u, { I64, I64, I64, I64, I64 }, 0u },
        { "fin_subpool_finalize", NEOS_RT_FUNCTION(neos_ada_fin_subpool_finalize), VOID, 1u, { I64 }, 0u },
        { "fin_unregister", NEOS_RT_FUNCTION(neos_ada_fin_unregister), VOID, 1u, { I64 }, 0u },
        { "fin_release", NEOS_RT_FUNCTION(neos_ada_fin_release), VOID, 1u, { I64 }, 0u },
    };
}
