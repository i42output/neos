/*
  objects.cpp

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

  Portions of this file are derived from CPython 3.13 (Objects/floatobject.c, Objects/complexobject.c,
  Objects/longobject.c, Objects/rangeobject.c, Objects/sliceobject.c, Objects/tupleobject.c, Objects/exceptions.c,
  Python/bltinmodule.c, Python/pyhash.c, Modules/mathmodule.c and Modules/cmathmodule.c), Copyright (c) 2001-2024 Python
  Software Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from C to C++ for the neos Python runtime plugin and adapted to its
  object model (see runtime/python/NOTICE).
*/

/*
  Python's core objects (None, bool, int, float, complex, str, list, tuple, dict, slice, range, iterators and builtin
  functions) and the object protocol (see neos/runtime/python/object.h): the operations that scripts perform on
  objects are dispatched to the objects' types, which may be those of other plugins (e.g. numpy).

  Limitation: an int is 64-bit (an operation whose result does not fit raises OverflowError).
*/

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cerrno>
#include <climits>
#include <charconv>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <map>
#include <unordered_map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#if defined(_MSC_VER)
#include <intrin.h>
#endif
#include <memory>
#include <neonumeric/integer.hpp>
#include <neonumeric/vecarray.hpp>
#include <neos/runtime/python/object.h>
#include "python.hpp"
#include "objects.hpp"

namespace neos::python_runtime
{
    namespace detail
    {

        // --- errors

        std::nullptr_t error(std::string const& aType, std::string const& aMessage)
        {
            python_runtime::raise(aType, aMessage);
            return nullptr;
        }

        char const* exception_type_name(object* aObject); // (an exception's class: else null)
        char const* instance_class_name(object* aObject); // (an instance's (user) class: else null)

        char const* type_name(object* aObject)
        {
            if (aObject == nullptr)
                return "NULL";
            if (auto const name = exception_type_name(aObject))
                return name;
            if (auto const name = instance_class_name(aObject))
                return name;
            if (auto const type = python_type(aObject))
                return type->name;
            return aObject->type->name;
        }

        // --- integers beyond 64 bits (neonumeric's arbitrary precision integers)

        bool is_big(object* aObject)
        {
            return aObject != nullptr && aObject->type == &sIntType.base && (as<int_object>(aObject)->big != nullptr || as<int_object>(aObject)->wide || 
                as<int_object>(aObject)->limbs != nullptr);
        }

        // --- integers of 128 bits (an int beyond 64 bits that fits: its value is its object's (wide), not a big integer's, so that 
        // arithmetic on it is native; a big integer is made from it when one is needed (see big_of))
        struct int128
        {
            std::uint64_t lo = 0u;
            std::int64_t hi = 0;
        };

        int128 int128_of(long long aValue)
        {
            return { static_cast<std::uint64_t>(aValue), aValue < 0 ? -1 : 0 };
        }

        // (an int or bool that is a 128-bit integer)
        bool int128_value(object* aObject, int128& aResult)
        {
            if (aObject == nullptr)
                return false;
            if (aObject->type == &sIntType.base)
            {
                auto const& i = *as<int_object>(aObject);
                if (i.wide)
                    aResult = { static_cast<std::uint64_t>(i.value), i.high };
                else if (i.big == nullptr && i.limbs == nullptr)
                    aResult = int128_of(i.value);
                else
                    return false;
                return true;
            }
            if (aObject == &sTrue.base || aObject == &sFalse.base)
            {
                aResult = int128_of(aObject == &sTrue.base ? 1 : 0);
                return true;
            }
            return false;
        }

        object* make_int128(int128 aValue)
        {
            if (aValue.hi == (static_cast<std::int64_t>(aValue.lo) < 0 ? -1 : 0))
                return make_int(static_cast<long long>(aValue.lo));
            auto* const result = new int_object{ { { &sIntType.base } }, static_cast<long long>(aValue.lo) };
            result->high = aValue.hi;
            result->wide = true;
            return &result->h.base;
        }

        // (sums and differences: false if they overflow; products of 64-bit integers (exact))
        bool add128(int128 a, int128 b, int128& r)
        {
            r.lo = a.lo + b.lo;
            std::uint64_t const carry = r.lo < a.lo ? 1u : 0u;
            r.hi = static_cast<std::int64_t>(static_cast<std::uint64_t>(a.hi) + static_cast<std::uint64_t>(b.hi) + carry);
            return ((a.hi ^ r.hi) & (b.hi ^ r.hi)) >= 0;
        }

        bool subtract128(int128 a, int128 b, int128& r)
        {
            r.lo = a.lo - b.lo;
            std::uint64_t const borrow = a.lo < b.lo ? 1u : 0u;
            r.hi = static_cast<std::int64_t>(static_cast<std::uint64_t>(a.hi) - static_cast<std::uint64_t>(b.hi) - borrow);
            return ((a.hi ^ b.hi) & (a.hi ^ r.hi)) >= 0;
        }

        int128 multiply64(long long a, long long b)
        {
#if defined(__SIZEOF_INT128__)
            auto const p = static_cast<__int128>(a) * static_cast<__int128>(b);
            return { static_cast<std::uint64_t>(p), static_cast<std::int64_t>(p >> 64) };
#else
            // (the magnitudes' product (of 32-bit halves), negated if the signs differ)
            auto const ua = a < 0 ? 0u - static_cast<std::uint64_t>(a) : static_cast<std::uint64_t>(a);
            auto const ub = b < 0 ? 0u - static_cast<std::uint64_t>(b) : static_cast<std::uint64_t>(b);
            std::uint64_t const a0 = ua & 0xFFFFFFFFu, a1 = ua >> 32u, b0 = ub & 0xFFFFFFFFu, b1 = ub >> 32u;
            std::uint64_t const p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
            std::uint64_t const middle = (p00 >> 32u) + (p01 & 0xFFFFFFFFu) + (p10 & 0xFFFFFFFFu);
            std::uint64_t lo = (p00 & 0xFFFFFFFFu) | (middle << 32u);
            std::uint64_t hi = p11 + (p01 >> 32u) + (p10 >> 32u) + (middle >> 32u);
            if ((a < 0) != (b < 0))
            {
                lo = ~lo + 1u;
                hi = ~hi + (lo == 0u ? 1u : 0u);
            }
            return { lo, static_cast<std::int64_t>(hi) };
#endif
        }

        int compare128(int128 a, int128 b)
        {
            if (a.hi != b.hi)
                return a.hi < b.hi ? -1 : 1;
            return a.lo < b.lo ? -1 : a.lo > b.lo ? 1 : 0;
        }

        // (+, - and * of ints that are 128-bit integers (and whose result is): null if not)
        object* int128_binary(int aOperation, object* aLhs, object* aRhs)
        {
            int128 a, b, r;
            if ((aOperation != NEOS_PY_ADD && aOperation != NEOS_PY_SUBTRACT && aOperation != NEOS_PY_MULTIPLY) || !int128_value(aLhs, a) || !int128_value(aRhs, b))
                return nullptr;
            if (aOperation == NEOS_PY_ADD)
                return add128(a, b, r) ? make_int128(r) : nullptr;
            if (aOperation == NEOS_PY_SUBTRACT)
                return subtract128(a, b, r) ? make_int128(r) : nullptr;
            // (a product: if its factors are 64-bit integers)
            auto const fits = [](int128 const& v) { return v.hi == (static_cast<std::int64_t>(v.lo) < 0 ? -1 : 0); };
            if (!fits(a) || !fits(b))
                return nullptr;
            return make_int128(multiply64(static_cast<long long>(a.lo), static_cast<long long>(b.lo)));
        }

        // --- ints beyond 128 bits: their magnitudes' 64-bit words (limbs, least significant first) and their signs (int_object::limbs, 
        // size and negative): arithmetic (Karatsuba's products, Knuth's divisions), shifts, bitwise operations, comparisons, hashes, 
        // conversions (to and from text, to bytes and floats) and math's gcd (Lehmer's), isqrt, factorial, comb and perm are native (as 
        // CPython's longobject.c's); the rest are neonumeric's (a big integer is made from them when one is needed: see big_of)
        using limb = std::uint64_t;

        // (an int's limbs while it is made (they are its own when it is made: see make_limbs))
        class limb_array
        {
        public:
            explicit limb_array(std::uint32_t aCount) : iCapacity{ aCount }, iData{ allocate_limbs(iCapacity) } {}
            limb_array(limb_array&& aOther) noexcept : iCapacity{ aOther.iCapacity }, iData{ aOther.iData } { aOther.iData = nullptr; }
            limb_array(limb_array const&) = delete;
            ~limb_array() { if (iData != nullptr) free_limbs(iData, iCapacity); }
        public:
            limb* get() const { return iData; }
            limb& operator[](std::size_t aIndex) const { return iData[aIndex]; }
            std::uint32_t capacity() const { return iCapacity; }
            limb* release() { auto const result = iData; iData = nullptr; return result; }
        private:
            std::uint32_t iCapacity;
            limb* iData;
        };

        // (an int's magnitude and sign: its limbs, or a 64-bit or 128-bit int's (in local))
        struct limb_view
        {
            limb const* data = nullptr;
            std::uint32_t size = 0u;
            bool negative = false;
            limb local[2] = {};
            limb_view() = default;
            limb_view(limb_view const&) = delete;
        };

        void view_of(int128 v, limb_view& aView);

        bool limbs_of(object* aObject, limb_view& aView)
        {
            if (aObject == nullptr)
                return false;
            int128 v;
            if (aObject->type == &sIntType.base)
            {
                auto const& i = *as<int_object>(aObject);
                if (i.limbs != nullptr)
                {
                    aView.data = i.limbs;
                    aView.size = i.size;
                    aView.negative = i.negative;
                    return true;
                }
                if (i.wide)
                    v = { static_cast<std::uint64_t>(i.value), i.high };
                else if (i.big == nullptr)
                    v = int128_of(i.value);
                else
                    return false;
            }
            else if (aObject == &sTrue.base || aObject == &sFalse.base)
                v = int128_of(aObject == &sTrue.base ? 1 : 0);
            else
                return false;
            view_of(v, aView);
            return true;
        }

        void view_of(int128 v, limb_view& aView)
        {
            aView.negative = v.hi < 0;
            auto lo = v.lo;
            auto hi = static_cast<limb>(v.hi);
            if (aView.negative)
            {
                lo = ~lo + 1u;
                hi = ~hi + (lo == 0u ? 1u : 0u);
            }
            aView.local[0] = lo;
            aView.local[1] = hi;
            aView.size = hi != 0u ? 2u : lo != 0u ? 1u : 0u;
            aView.data = aView.local;
        }

        // (a * b + c + d: its low word, and its high word in aHigh (it fits in 128 bits))
        limb multiply_add(limb a, limb b, limb c, limb d, limb& aHigh)
        {
#if defined(__SIZEOF_INT128__)
            auto const r = static_cast<unsigned __int128>(a) * b + c + d;
            aHigh = static_cast<limb>(r >> 64);
            return static_cast<limb>(r);
#else
            limb const a0 = a & 0xFFFFFFFFu, a1 = a >> 32u, b0 = b & 0xFFFFFFFFu, b1 = b >> 32u;
            limb const p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
            limb const middle = (p00 >> 32u) + (p01 & 0xFFFFFFFFu) + (p10 & 0xFFFFFFFFu);
            limb lo = (p00 & 0xFFFFFFFFu) | (middle << 32u);
            limb hi = p11 + (p01 >> 32u) + (p10 >> 32u) + (middle >> 32u);
            lo += c;
            hi += lo < c ? 1u : 0u;
            lo += d;
            hi += lo < d ? 1u : 0u;
            aHigh = hi;
            return lo;
#endif
        }

        int compare_limbs(limb const* a, std::uint32_t an, limb const* b, std::uint32_t bn)
        {
            if (an != bn)
                return an < bn ? -1 : 1;
            for (auto i = an; i-- > 0u;)
                if (a[i] != b[i])
                    return a[i] < b[i] ? -1 : 1;
            return 0;
        }

        // (|a| + |b| (an >= bn) in r (an + 1 limbs))
        std::uint32_t add_limbs(limb const* a, std::uint32_t an, limb const* b, std::uint32_t bn, limb* r)
        {
            limb carry = 0u;
            std::uint32_t i = 0u;
            for (; i < bn; ++i)
            {
#if defined(__SIZEOF_INT128__)
                auto const s = static_cast<unsigned __int128>(a[i]) + b[i] + carry;
                r[i] = static_cast<limb>(s);
                carry = static_cast<limb>(s >> 64);
#else
                auto const s = a[i] + carry;
                carry = s < carry ? 1u : 0u;
                r[i] = s + b[i];
                carry += r[i] < s ? 1u : 0u;
#endif
            }
            for (; i < an && carry != 0u; ++i)
            {
                r[i] = a[i] + carry;
                carry = r[i] < carry ? 1u : 0u;
            }
            std::copy(a + i, a + an, r + i);
            r[an] = carry;
            return an + 1u;
        }

        // (|a| - |b| (|a| >= |b|) in r (an limbs))
        std::uint32_t subtract_limbs(limb const* a, std::uint32_t an, limb const* b, std::uint32_t bn, limb* r)
        {
            limb borrow = 0u;
            std::uint32_t i = 0u;
            for (; i < bn; ++i)
            {
                auto const d = a[i] - b[i];
                auto const nextBorrow = (a[i] < b[i] ? 1u : 0u) + (d < borrow ? 1u : 0u);
                r[i] = d - borrow;
                borrow = nextBorrow;
            }
            for (; i < an && borrow != 0u; ++i)
            {
                r[i] = a[i] - borrow;
                borrow = a[i] < borrow ? 1u : 0u;
            }
            std::copy(a + i, a + an, r + i);
            return an;
        }

        // (an int from a magnitude (its limbs: adopted) and a sign: a 64-bit or 128-bit int if it fits)
        object* make_limbs(limb_array aLimbs, std::uint32_t aSize, bool aNegative)
        {
            while (aSize > 0u && aLimbs[aSize - 1u] == 0u)
                --aSize;
            if (aSize == 0u)
                return make_int(0);
            if (aSize <= 2u)
            {
                auto const lo = aLimbs[0];
                auto const hi = aSize > 1u ? aLimbs[1] : limb{ 0u };
                constexpr limb top = limb{ 1u } << 63u;
                if (hi < top || (aNegative && hi == top && lo == 0u))
                {
                    int128 v{ lo, static_cast<std::int64_t>(hi) };
                    if (aNegative)
                    {
                        v.lo = ~lo + 1u;
                        v.hi = static_cast<std::int64_t>(~hi + (v.lo == 0u ? 1u : 0u));
                    }
                    return make_int128(v);
                }
            }
            auto* const result = new int_object{ { { &sIntType.base } }, 0 };
            result->capacity = aLimbs.capacity();
            result->limbs = aLimbs.release();
            result->size = aSize;
            result->negative = aNegative;
            return &result->h.base;
        }

        object* add_views(limb_view const& a, limb_view const& b, bool aSubtract)
        {
            bool const bNegative = (b.negative != aSubtract) && b.size != 0u;
            bool const aNegative = a.negative && a.size != 0u;
            auto const n = std::max(a.size, b.size) + 1u;
            limb_array r{ n };
            if (aNegative == bNegative || a.size == 0u || b.size == 0u)
            {
                bool const negative = a.size != 0u ? aNegative : bNegative;
                auto const size = a.size >= b.size ? add_limbs(a.data, a.size, b.data, b.size, r.get()) : add_limbs(b.data, b.size, a.data, a.size, r.get());
                return make_limbs(std::move(r), size, negative);
            }
            auto const order = compare_limbs(a.data, a.size, b.data, b.size);
            if (order == 0)
                return make_int(0);
            if (order > 0)
                return make_limbs(std::move(r), subtract_limbs(a.data, a.size, b.data, b.size, r.get()), aNegative);
            return make_limbs(std::move(r), subtract_limbs(b.data, b.size, a.data, a.size, r.get()), bNegative);
        }

        // (r += x (r: rn limbs, x: xn <= rn limbs; the sum fits))
        void add_into(limb* r, std::uint32_t rn, limb const* x, std::uint32_t xn)
        {
            limb carry = 0u;
            std::uint32_t i = 0u;
            for (; i < xn; ++i)
            {
                auto const s = r[i] + carry;
                carry = s < carry ? 1u : 0u;
                r[i] = s + x[i];
                carry += r[i] < s ? 1u : 0u;
            }
            for (; carry != 0u && i < rn; ++i)
                carry = ++r[i] == 0u ? 1u : 0u;
        }

        // (r -= x (r: rn limbs, x: xn <= rn limbs; r >= x))
        void subtract_from(limb* r, std::uint32_t rn, limb const* x, std::uint32_t xn)
        {
            limb borrow = 0u;
            std::uint32_t i = 0u;
            for (; i < xn; ++i)
            {
                auto const d = r[i] - x[i];
                auto const nextBorrow = (r[i] < x[i] ? 1u : 0u) + (d < borrow ? 1u : 0u);
                r[i] = d - borrow;
                borrow = nextBorrow;
            }
            for (; borrow != 0u && i < rn; ++i)
                borrow = r[i]-- == 0u ? 1u : 0u;
        }

        // (a * b in r (an + bn limbs): schoolbook, and Karatsuba's for factors of 40 limbs or more (as CPython's k_mul))
        void multiply_limbs(limb const* a, std::uint32_t an, limb const* b, std::uint32_t bn, limb* r)
        {
            constexpr std::uint32_t cutoff = 40u;
            if (an < bn)
            {
                std::swap(a, b);
                std::swap(an, bn);
            }
            std::fill(r, r + an + bn, limb{ 0u });
            if (bn < cutoff)
            {
                for (std::uint32_t i = 0u; i < bn; ++i)
                {
                    limb carry = 0u;
                    auto const bi = b[i];
                    for (std::uint32_t j = 0u; j < an; ++j)
                        r[i + j] = multiply_add(bi, a[j], r[i + j], carry, carry);
                    r[i + an] = carry;
                }
                return;
            }
            auto const h = (an + 1u) / 2u;
            if (bn <= h)
            {
                // (a much longer factor: in slices of the other's size)
                std::vector<limb> t(2u * bn);
                for (std::uint32_t offset = 0u; offset < an; offset += bn)
                {
                    auto const length = std::min(bn, an - offset);
                    multiply_limbs(a + offset, length, b, bn, t.data());
                    add_into(r + offset, an + bn - offset, t.data(), length + bn);
                }
                return;
            }
            // (a = a1 B^h + a0, b = b1 B^h + b0: a b = z2 B^2h + ((a0 + a1)(b0 + b1) - z0 - z2) B^h + z0)
            multiply_limbs(a, h, b, h, r);
            multiply_limbs(a + h, an - h, b + h, bn - h, r + 2u * h);
            std::vector<limb> sa(h + 1u, 0u), sb(h + 1u, 0u), z1(2u * h + 2u);
            std::copy(a, a + h, sa.begin());
            add_into(sa.data(), h + 1u, a + h, an - h);
            std::copy(b, b + h, sb.begin());
            add_into(sb.data(), h + 1u, b + h, bn - h);
            auto san = h + 1u, sbn = h + 1u;
            while (san > 0u && sa[san - 1u] == 0u)
                --san;
            while (sbn > 0u && sb[sbn - 1u] == 0u)
                --sbn;
            std::fill(z1.begin(), z1.end(), limb{ 0u });
            if (san != 0u && sbn != 0u)
                multiply_limbs(sa.data(), san, sb.data(), sbn, z1.data());
            auto const zn = 2u * h + 2u;
            subtract_from(z1.data(), zn, r, 2u * h);
            subtract_from(z1.data(), zn, r + 2u * h, an + bn - 2u * h);
            auto z1n = zn;
            while (z1n > 0u && z1[z1n - 1u] == 0u)
                --z1n;
            add_into(r + h, an + bn - h, z1.data(), z1n);
        }

        object* multiply_views(limb_view const& a, limb_view const& b)
        {
            if (a.size == 0u || b.size == 0u)
                return make_int(0);
            auto const n = a.size + b.size;
            limb_array r{ n };
            // (by a single limb (e.g. a factorial's): one pass)
            if (a.size == 1u || b.size == 1u)
            {
                auto const& x = a.size == 1u ? b : a;
                auto const y = a.size == 1u ? a.data[0] : b.data[0];
                limb carry = 0u;
                for (std::uint32_t i = 0u; i < x.size; ++i)
                    r[i] = multiply_add(x.data[i], y, carry, 0u, carry);
                r[x.size] = carry;
                return make_limbs(std::move(r), x.size + 1u, a.negative != b.negative);
            }
            multiply_limbs(a.data, a.size, b.data, b.size, r.get());
            return make_limbs(std::move(r), n, a.negative != b.negative);
        }

        // (a 128-bit dividend's quotient (aHigh < aDivisor: it fits in 64 bits) and remainder)
        limb divide_128(limb aHigh, limb aLow, limb aDivisor, limb& aRemainder)
        {
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
            // (the instruction itself (unsigned __int128's division is a library call's))
            limb quotient = 0u, remainder = 0u;
            __asm__("divq %4" : "=a"(quotient), "=d"(remainder) : "a"(aLow), "d"(aHigh), "rm"(aDivisor) : "cc");
            aRemainder = remainder;
            return quotient;
#elif defined(__SIZEOF_INT128__)
            auto const dividend = (static_cast<unsigned __int128>(aHigh) << 64) | aLow;
            aRemainder = static_cast<limb>(dividend % aDivisor);
            return static_cast<limb>(dividend / aDivisor);
#elif defined(_MSC_VER) && defined(_M_X64)
            return _udiv128(aHigh, aLow, aDivisor, &aRemainder);
#else
            // (a bit at a time)
            limb quotient = 0u;
            limb remainder = aHigh;
            for (int bit = 63; bit >= 0; --bit)
            {
                bool const carry = (remainder >> 63u) != 0u;
                remainder = (remainder << 1u) | ((aLow >> bit) & 1u);
                quotient <<= 1u;
                if (carry || remainder >= aDivisor)
                {
                    remainder -= aDivisor;
                    quotient |= 1u;
                }
            }
            aRemainder = remainder;
            return quotient;
#endif
        }

        // (a magnitude divided by a single limb: its quotient (in q: an limbs) and remainder)
        limb divide_limbs(limb const* a, std::uint32_t an, limb aDivisor, limb* q)
        {
            limb remainder = 0u;
            for (auto i = an; i-- > 0u;)
                q[i] = divide_128(remainder, a[i], aDivisor, remainder);
            return remainder;
        }

        // (a magnitude (u: n limbs) divided by another (v: m limbs, 2 <= m <= n; its most significant not zero): its quotient (in q: 
        // n - m + 1 limbs) and remainder (in r: m limbs) (Knuth's algorithm D, as Hacker's Delight's divmnu))
        void divide_limbs(limb const* u, std::uint32_t n, limb const* v, std::uint32_t m, limb* q, limb* r)
        {
            auto const s = static_cast<unsigned>(std::countl_zero(v[m - 1u]));
            auto const shifted = [s](limb aHigh, limb aLow) { return s == 0u ? aHigh : (aHigh << s) | (aLow >> (64u - s)); };
            // (scratch kept for reuse (a division is made per step of a pow(base, exponent, modulus)))
            thread_local std::vector<limb> vn, un;
            vn.resize(m);
            un.resize(n + 1u);
            for (auto i = m - 1u; i > 0u; --i)
                vn[i] = shifted(v[i], v[i - 1u]);
            vn[0] = v[0] << s;
            un[n] = s == 0u ? 0u : u[n - 1u] >> (64u - s);
            for (auto i = n - 1u; i > 0u; --i)
                un[i] = shifted(u[i], u[i - 1u]);
            un[0] = u[0] << s;
            for (auto j = n - m + 1u; j-- > 0u;)
            {
                // (the quotient's digit: estimated from the leading digits, then corrected)
                limb qhat = 0u, rhat = 0u;
                bool rhatOverflow = false;
                if (un[j + m] >= vn[m - 1u])
                {
                    qhat = ~limb{ 0u };
                    rhat = un[j + m - 1u] + vn[m - 1u];
                    rhatOverflow = rhat < vn[m - 1u];
                }
                else
                    qhat = divide_128(un[j + m], un[j + m - 1u], vn[m - 1u], rhat);
                while (!rhatOverflow)
                {
                    limb high = 0u;
                    auto const low = multiply_add(qhat, vn[m - 2u], 0u, 0u, high);
                    if (high < rhat || (high == rhat && low <= un[j + m - 2u]))
                        break;
                    --qhat;
                    rhat += vn[m - 1u];
                    rhatOverflow = rhat < vn[m - 1u];
                }
                // (its multiple of the divisor subtracted)
                limb carry = 0u, borrow = 0u;
                for (std::uint32_t i = 0u; i < m; ++i)
                {
                    limb high = 0u;
                    auto const product = multiply_add(qhat, vn[i], carry, 0u, high);
                    carry = high;
                    auto const t = un[i + j] - product;
                    auto const b1 = un[i + j] < product ? 1u : 0u;
                    auto const t2 = t - borrow;
                    auto const b2 = t < borrow ? 1u : 0u;
                    un[i + j] = t2;
                    borrow = b1 + b2;
                }
                auto const t = un[j + m] - carry;
                auto const b1 = un[j + m] < carry;
                auto const t2 = t - borrow;
                auto const b2 = t < borrow;
                un[j + m] = t2;
                q[j] = qhat;
                if (b1 || b2)
                {
                    // (one too many: the divisor added back)
                    --q[j];
                    limb c = 0u;
                    for (std::uint32_t i = 0u; i < m; ++i)
                    {
                        auto const s1 = un[i + j] + c;
                        auto const c1 = s1 < c ? 1u : 0u;
                        auto const s2 = s1 + vn[i];
                        auto const c2 = s2 < s1 ? 1u : 0u;
                        un[i + j] = s2;
                        c = c1 + c2;
                    }
                    un[j + m] += c;
                }
            }
            for (std::uint32_t i = 0u; i < m; ++i)
                r[i] = s == 0u ? un[i] : (un[i] >> s) | (un[i + 1u] << (64u - s));
        }

        // (a magnitude's quotient and remainder by another (not zero): q (an - bn + 1 limbs, or 1 if an < bn) and r (bn limbs))
        void divide_magnitudes(limb const* a, std::uint32_t an, limb const* b, std::uint32_t bn, limb* q, limb* r)
        {
            if (an < bn)
            {
                q[0] = 0u;
                std::copy(a, a + an, r);
                std::fill(r + an, r + bn, limb{ 0u });
            }
            else if (bn == 1u)
                r[0] = divide_limbs(a, an, b[0], q);
            else
                divide_limbs(a, an, b, bn, q, r);
        }

        // (// and % of ints (as Python's: the quotient is floored, the remainder has the divisor's sign); the divisor is not zero)
        object* divide_views(limb_view const& a, limb_view const& d, bool aRemainder, object** aRemainderResult = nullptr)
        {
            auto const qn = a.size >= d.size ? a.size - d.size + 2u : 2u;
            limb_array q{ qn };
            limb_array r{ d.size + 1u };
            std::fill(q.get(), q.get() + qn, limb{ 0u });
            r[d.size] = 0u;
            divide_magnitudes(a.data, a.size, d.data, d.size, q.get(), r.get());
            bool const aNegative = a.negative && a.size != 0u;
            bool const differ = aNegative != d.negative;
            bool remainderZero = true;
            for (std::uint32_t i = 0u; i < d.size; ++i)
                if (r[i] != 0u)
                    remainderZero = false;
            bool const adjust = differ && !remainderZero;
            if (aRemainder || aRemainderResult != nullptr)
            {
                // (the remainder: |d| - r with the divisor's sign (if it is adjusted), else r with the dividend's)
                if (adjust)
                    subtract_limbs(d.data, d.size, r.get(), d.size, r.get());
                auto const remainder = make_limbs(std::move(r), d.size, adjust ? d.negative : aNegative);
                if (aRemainderResult == nullptr)
                    return remainder;
                // (divmod: both)
                *aRemainderResult = remainder;
            }
            if (adjust)
            {
                // (the quotient's magnitude + 1)
                for (std::uint32_t i = 0u; i < qn; ++i)
                    if (++q[i] != 0u)
                        break;
            }
            return make_limbs(std::move(q), qn, differ);
        }

        // (a magnitude (std::vector: its limbs) without its leading zeros)
        using magnitude = std::vector<limb>;

        void trim(magnitude& aValue)
        {
            while (!aValue.empty() && aValue.back() == 0u)
                aValue.pop_back();
        }

        magnitude multiply_magnitudes(magnitude const& a, magnitude const& b)
        {
            if (a.empty() || b.empty())
                return {};
            magnitude r(a.size() + b.size());
            multiply_limbs(a.data(), static_cast<std::uint32_t>(a.size()), b.data(), static_cast<std::uint32_t>(b.size()), r.data());
            trim(r);
            return r;
        }

        magnitude modulo_magnitude(magnitude const& a, magnitude const& m)
        {
            if (compare_limbs(a.data(), static_cast<std::uint32_t>(a.size()), m.data(), static_cast<std::uint32_t>(m.size())) < 0)
                return a;
            magnitude q(a.size() - m.size() + 2u, 0u), r(m.size() + 1u, 0u);
            divide_magnitudes(a.data(), static_cast<std::uint32_t>(a.size()), m.data(), static_cast<std::uint32_t>(m.size()), q.data(), r.data());
            trim(r);
            return r;
        }

        object* magnitude_int(magnitude const& aValue, bool aNegative)
        {
            limb_array result{ static_cast<std::uint32_t>(std::max<std::size_t>(aValue.size(), 1u)) };
            result[0] = 0u;
            std::copy(aValue.begin(), aValue.end(), result.get());
            return make_limbs(std::move(result), static_cast<std::uint32_t>(aValue.size()), aNegative);
        }

        // (** of an int and an exponent of 64 bits (not negative): squares and products)
        object* power_views(limb_view const& aBase, limb aExponent)
        {
            magnitude base{ aBase.data, aBase.data + aBase.size };
            magnitude result{ 1u };
            bool const negative = aBase.negative && aBase.size != 0u && (aExponent & 1u) != 0u;
            for (auto e = aExponent; e != 0u; e >>= 1u)
            {
                if ((e & 1u) != 0u)
                    result = multiply_magnitudes(result, base);
                if (e > 1u)
                    base = multiply_magnitudes(base, base);
            }
            return magnitude_int(result, negative);
        }

        // (a * b mod m (a and b less than m, which has mn limbs) in out (mn limbs; it can be a or b): its size (product: an + bn limbs, 
        // quotient: mn + 2))
        std::uint32_t multiply_modulo(limb const* a, std::uint32_t an, limb const* b, std::uint32_t bn, limb const* m, std::uint32_t mn, 
            limb* product, limb* quotient, limb* out)
        {
            if (an == 0u || bn == 0u)
                return 0u;
            std::fill(product, product + an + bn, limb{ 0u });
            for (std::uint32_t i = 0u; i < an; ++i)
            {
                limb carry = 0u;
                for (std::uint32_t j = 0u; j < bn; ++j)
                    product[i + j] = multiply_add(a[i], b[j], product[i + j], carry, carry);
                product[i + bn] = carry;
            }
            auto pn = an + bn;
            while (pn > 0u && product[pn - 1u] == 0u)
                --pn;
            if (compare_limbs(product, pn, m, mn) < 0)
            {
                std::copy(product, product + pn, out);
                return pn;
            }
            divide_magnitudes(product, pn, m, mn, quotient, out);
            auto rn = mn;
            while (rn > 0u && out[rn - 1u] == 0u)
                --rn;
            return rn;
        }

        // (pow(base, exponent, modulus) of ints (the exponent is not negative, the modulus is not zero): the result has the modulus's sign)
        object* power_modulo_views(limb_view const& aBase, limb_view const& aExponent, limb_view const& aModulus)
        {
            magnitude const m{ aModulus.data, aModulus.data + aModulus.size };
            magnitude base = modulo_magnitude(magnitude{ aBase.data, aBase.data + aBase.size }, m);
            if (aBase.negative && !base.empty())
            {
                // (the base's floored remainder: |m| - (|base| mod |m|))
                magnitude difference(m.size(), 0u);
                subtract_limbs(m.data(), static_cast<std::uint32_t>(m.size()), base.data(), static_cast<std::uint32_t>(base.size()), difference.data());
                trim(difference);
                base = std::move(difference);
            }
            // (left to right from the exponent's top bit (the result is then the base), with buffers made once)
            auto const mn = static_cast<std::uint32_t>(m.size());
            auto const bn = static_cast<std::uint32_t>(base.size());
            magnitude result(mn + 1u, 0u), product(2u * mn + 1u), quotient(mn + 2u);
            std::uint32_t rn = mn == 1u && m[0] == 1u ? 0u : 1u;
            result[0] = rn;
            bool started = false;
            for (auto i = aExponent.size; i-- > 0u;)
                for (int bit = 63; bit >= 0; --bit)
                {
                    bool const set = ((aExponent.data[i] >> bit) & 1u) != 0u;
                    if (!started)
                    {
                        if (set)
                        {
                            started = true;
                            std::copy(base.begin(), base.end(), result.begin());
                            rn = bn;
                        }
                        continue;
                    }
                    rn = multiply_modulo(result.data(), rn, result.data(), rn, m.data(), mn, product.data(), quotient.data(), result.data());
                    if (set)
                        rn = multiply_modulo(result.data(), rn, base.data(), bn, m.data(), mn, product.data(), quotient.data(), result.data());
                }
            result.resize(rn);
            if (aModulus.negative && !result.empty())
            {
                magnitude difference(m.size(), 0u);
                subtract_limbs(m.data(), static_cast<std::uint32_t>(m.size()), result.data(), static_cast<std::uint32_t>(result.size()), difference.data());
                trim(difference);
                return magnitude_int(difference, true);
            }
            return magnitude_int(result, false);
        }

        bool round_to_double(std::uint64_t top, bool sticky, std::int64_t e, bool aNegative, double& aResult);

        // (the bits of a magnitude (its top limb not zero))
        std::uint64_t limbs_bits(limb const* a, std::uint32_t n)
        {
            return n == 0u ? 0u : (n - 1u) * std::uint64_t{ 64u } + (64u - static_cast<unsigned>(std::countl_zero(a[n - 1u])));
        }

        // (a magnitude (its top limb not zero) * 2^aExponent correctly rounded to a double (as big_to_double); false if it overflows)
        bool limbs_to_double(limb const* a, std::uint32_t n, std::int64_t aExponent, bool aSticky, bool aNegative, double& aResult)
        {
            auto const bits = static_cast<std::int64_t>(limbs_bits(a, n));
            if (bits == 0)
            {
                aResult = aNegative ? -0.0 : 0.0;
                return true;
            }
            // (its top 64 bits, and whether a bit below them is set)
            auto const low = bits - 64;
            std::uint64_t top = 0u;
            bool sticky = aSticky;
            if (low <= 0)
                top = a[0] << static_cast<unsigned>(-low);
            else
            {
                auto const w = static_cast<std::uint32_t>(low / 64);
                auto const shift = static_cast<unsigned>(low % 64);
                top = shift == 0u ? a[w] : (a[w] >> shift) | (a[w + 1u] << (64u - shift));
                sticky = sticky || (shift != 0u && (a[w] & ((limb{ 1u } << shift) - 1u)) != 0u);
                for (std::uint32_t i = 0u; i < w && !sticky; ++i)
                    sticky = a[i] != 0u;
            }
            return round_to_double(top, sticky, bits - 1 + aExponent, aNegative, aResult);
        }

        // (a magnitude << s in out (an + s / 64 + 1 limbs, which it returns))
        std::uint32_t shift_magnitude(limb const* a, std::uint32_t an, std::uint64_t s, limb* out)
        {
            auto const words = static_cast<std::uint32_t>(s / 64u);
            auto const bits = static_cast<unsigned>(s % 64u);
            std::fill(out, out + words, limb{ 0u });
            limb carry = 0u;
            for (std::uint32_t i = 0u; i < an; ++i)
            {
                out[words + i] = bits == 0u ? a[i] : (a[i] << bits) | carry;
                carry = bits == 0u ? 0u : a[i] >> (64u - bits);
            }
            out[words + an] = carry;
            return an + words + 1u;
        }

        // (an int << n)
        object* shift_left_views(limb_view const& a, std::uint64_t n)
        {
            if (a.size == 0u)
                return make_int(0);
            auto const size = a.size + static_cast<std::uint32_t>(n / 64u) + 1u;
            limb_array r{ size };
            shift_magnitude(a.data, a.size, n, r.get());
            return make_limbs(std::move(r), size, a.negative);
        }

        // (an int >> n (floored, as Python's))
        object* shift_right_views(limb_view const& a, std::uint64_t n)
        {
            bool const negative = a.negative && a.size != 0u;
            if (n >= limbs_bits(a.data, a.size))
                return make_int(negative ? -1 : 0);
            auto const words = static_cast<std::uint32_t>(n / 64u);
            auto const bits = static_cast<unsigned>(n % 64u);
            auto const size = a.size - words;
            limb_array r{ size + 1u };
            for (std::uint32_t i = 0u; i < size; ++i)
                r[i] = bits == 0u ? a.data[words + i] : (a.data[words + i] >> bits) | (words + i + 1u < a.size ? a.data[words + i + 1u] << (64u - bits) : limb{ 0u });
            r[size] = 0u;
            // (a negative int's bits shifted out: its magnitude's quotient rounded up)
            bool lost = false;
            if (negative)
            {
                lost = bits != 0u && (a.data[words] & ((limb{ 1u } << bits) - 1u)) != 0u;
                for (std::uint32_t i = 0u; i < words && !lost; ++i)
                    lost = a.data[i] != 0u;
            }
            for (std::uint32_t i = 0u; lost && i <= size; ++i)
                lost = ++r[i] == 0u;
            return make_limbs(std::move(r), size + 1u, negative);
        }

        // (&, | and ^ of ints (as two's complement integers, as Python's))
        object* bitwise_views(int aOperation, limb_view const& a, limb_view const& b)
        {
            // (of magnitudes (not negative): their limbs')
            if (!(a.negative && a.size != 0u) && !(b.negative && b.size != 0u))
            {
                auto const size = aOperation == NEOS_PY_AND ? std::min(a.size, b.size) : std::max(a.size, b.size);
                limb_array r{ std::max(size, 1u) };
                for (std::uint32_t i = 0u; i < size; ++i)
                {
                    auto const x = i < a.size ? a.data[i] : limb{ 0u };
                    auto const y = i < b.size ? b.data[i] : limb{ 0u };
                    r[i] = aOperation == NEOS_PY_AND ? x & y : aOperation == NEOS_PY_OR ? x | y : x ^ y;
                }
                return make_limbs(std::move(r), size, false);
            }
            auto const n = std::max(a.size, b.size) + 1u;
            limb_array x{ n }, y{ n }, r{ n };
            auto const negate = [n](limb* v)
            {
                limb carry = 1u;
                for (std::uint32_t i = 0u; i < n; ++i)
                {
                    v[i] = ~v[i] + carry;
                    carry = carry != 0u && v[i] == 0u ? 1u : 0u;
                }
            };
            auto const twos = [&](limb_view const& v, limb* out)
            {
                std::copy(v.data, v.data + v.size, out);
                std::fill(out + v.size, out + n, limb{ 0u });
                if (v.negative && v.size != 0u)
                    negate(out);
            };
            twos(a, x.get());
            twos(b, y.get());
            for (std::uint32_t i = 0u; i < n; ++i)
                r[i] = aOperation == NEOS_PY_AND ? x[i] & y[i] : aOperation == NEOS_PY_OR ? x[i] | y[i] : x[i] ^ y[i];
            bool const negative = (r[n - 1u] >> 63u) != 0u;
            if (negative)
                negate(r.get());
            return make_limbs(std::move(r), n, negative);
        }

        // (an int / an int (not zero), correctly rounded (as big_binary's): null if it is not native (its result is out of range))
        object* true_divide_views(limb_view const& a, limb_view const& b)
        {
            bool const negative = (a.negative && a.size != 0u) != (b.negative && b.size != 0u);
            if (a.size == 0u)
                return make_float(negative ? -0.0 : 0.0);
            auto const shift = static_cast<std::int64_t>(limbs_bits(a.data, a.size)) - static_cast<std::int64_t>(limbs_bits(b.data, b.size)) - 55;
            if (shift > 1100)
                return nullptr;
            if (shift < -1200)
                return make_float(negative ? -0.0 : 0.0);
            // (a / b = q * 2^shift (+ the remainder): q has 55 or 56 bits)
            auto const ns = static_cast<std::uint64_t>(shift < 0 ? -shift : 0);
            auto const ds = static_cast<std::uint64_t>(shift > 0 ? shift : 0);
            limb_array n{ a.size + static_cast<std::uint32_t>(ns / 64u) + 1u }, d{ b.size + static_cast<std::uint32_t>(ds / 64u) + 1u };
            auto nn = shift_magnitude(a.data, a.size, ns, n.get());
            auto dn = shift_magnitude(b.data, b.size, ds, d.get());
            while (nn > 0u && n[nn - 1u] == 0u)
                --nn;
            while (dn > 0u && d[dn - 1u] == 0u)
                --dn;
            auto const qn = nn >= dn ? nn - dn + 2u : 2u;
            limb_array q{ qn }, r{ dn + 1u };
            std::fill(q.get(), q.get() + qn, limb{ 0u });
            std::fill(r.get(), r.get() + dn + 1u, limb{ 0u });
            divide_magnitudes(n.get(), nn, d.get(), dn, q.get(), r.get());
            auto qs = qn;
            while (qs > 0u && q[qs - 1u] == 0u)
                --qs;
            bool sticky = false;
            for (std::uint32_t i = 0u; i < dn && !sticky; ++i)
                sticky = r[i] != 0u;
            double result = 0.0;
            if (!limbs_to_double(q.get(), qs, shift, sticky, negative, result))
                return nullptr;
            return make_float(result);
        }

        // (the greatest common divisor of ints (Lehmer's algorithm, as CPython's _PyLong_GCD (Objects/longobject.c, Python Software 
        // Foundation License), on 64-bit limbs))
        object* gcd_views(limb_view const& aA, limb_view const& aB)
        {
            magnitude a{ aA.data, aA.data + aA.size }, b{ aB.data, aB.data + aB.size }, c, d;
            auto const size = [](magnitude const& v) { return static_cast<std::uint32_t>(v.size()); };
            if (compare_limbs(a.data(), size(a), b.data(), size(b)) < 0)
                std::swap(a, b);
            // (P X - Q Y (not negative) in r)
            auto const combine = [](limb P, magnitude const& X, limb Q, magnitude const& Y, magnitude& r)
            {
                auto const n = std::max(X.size(), Y.size());
                r.resize(n);
                limb c1 = 0u, c2 = 0u, borrow = 0u;
                for (std::size_t i = 0u; i < n; ++i)
                {
                    auto const p = multiply_add(P, i < X.size() ? X[i] : limb{ 0u }, c1, 0u, c1);
                    auto const q = multiply_add(Q, i < Y.size() ? Y[i] : limb{ 0u }, c2, 0u, c2);
                    auto const t = p - q;
                    auto const b1 = p < q ? 1u : 0u;
                    r[i] = t - borrow;
                    borrow = b1 + (t < borrow ? 1u : 0u);
                }
                trim(r);
            };
            while (a.size() > 1u)
            {
                if (b.empty())
                    return magnitude_int(a, false);
                // (the top 60 bits of a, and b's bits in the same places)
                auto const shift = limbs_bits(a.data(), size(a)) - 60u;
                auto const top = [shift](magnitude const& v) -> std::int64_t
                {
                    auto const w = static_cast<std::size_t>(shift / 64u);
                    auto const bits = static_cast<unsigned>(shift % 64u);
                    if (w >= v.size())
                        return 0;
                    auto r = v[w] >> bits;
                    if (bits != 0u && w + 1u < v.size())
                        r |= v[w + 1u] << (64u - bits);
                    return static_cast<std::int64_t>(r & ((limb{ 1u } << 60u) - 1u));
                };
                std::int64_t x = top(a), y = top(b);
                // (the inner loop of Lehmer's algorithm: A, B, C and D do not exceed 2^30)
                std::int64_t A = 1, B = 0, C = 0, D = 1;
                int k = 0;
                for (;; ++k)
                {
                    if (y - C == 0)
                        break;
                    auto const q = (x + (A - 1)) / (y - C);
                    auto const s = B + q * D;
                    auto t = x - q * y;
                    if (s > t)
                        break;
                    x = y;
                    y = t;
                    t = A + q * C;
                    A = D;
                    B = C;
                    C = s;
                    D = t;
                }
                if (k == 0)
                {
                    // (no progress: a step of Euclid's (a, b = b, a % b))
                    magnitude q(a.size() - b.size() + 2u, 0u), r(b.size() + 1u, 0u);
                    divide_magnitudes(a.data(), size(a), b.data(), size(b), q.data(), r.data());
                    r.resize(b.size());
                    trim(r);
                    a = std::move(b);
                    b = std::move(r);
                    continue;
                }
                // (a, b = A b - B a, D a - C b if k is odd; A a - B b, D b - C a if k is even)
                auto const P = static_cast<limb>(A), Q = static_cast<limb>(B), R = static_cast<limb>(C), S = static_cast<limb>(D);
                if ((k & 1) != 0)
                {
                    combine(P, b, Q, a, c);
                    combine(S, a, R, b, d);
                }
                else
                {
                    combine(P, a, Q, b, c);
                    combine(S, b, R, a, d);
                }
                a.swap(c);
                b.swap(d);
            }
            // (64-bit integers: Euclid's)
            limb x = a.empty() ? 0u : a[0], y = b.empty() ? 0u : b[0];
            while (y != 0u)
            {
                auto const t = x % y;
                x = y;
                y = t;
            }
            return magnitude_int(magnitude{ x }, false);
        }

        // (an int from its digits (valid, in base 2 to 36) and its sign: a limb's worth of digits at a time)
        object* digits_int(std::string_view aDigits, unsigned aBase, bool aNegative)
        {
            unsigned chunk = 0u;
            for (limb power = 1u; power <= ~limb{ 0u } / aBase; power *= aBase)
                ++chunk;
            limb_array r{ static_cast<std::uint32_t>(aDigits.size() / chunk + 2u) };
            std::uint32_t size = 0u;
            for (std::size_t i = 0u; i < aDigits.size();)
            {
                auto const n = std::min<std::size_t>(chunk, aDigits.size() - i);
                limb value = 0u, scale = 1u;
                for (std::size_t j = 0u; j < n; ++j, ++i)
                {
                    auto const c = aDigits[i];
                    unsigned const d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'z' ? c - 'a' + 10 : c - 'A' + 10;
                    value = value * aBase + d;
                    scale *= aBase;
                }
                limb carry = value;
                for (std::uint32_t k = 0u; k < size; ++k)
                    r[k] = multiply_add(r[k], scale, carry, 0u, carry);
                if (carry != 0u)
                    r[size++] = carry;
            }
            return make_limbs(std::move(r), size, aNegative);
        }

        // (+, -, *, //, % and ** (an exponent of 64 bits, not negative) of ints, one of them beyond 128 bits: null if not native)
        object* limbs_binary(int aOperation, object* aLhs, object* aRhs)
        {
            if (aOperation != NEOS_PY_ADD && aOperation != NEOS_PY_SUBTRACT && aOperation != NEOS_PY_MULTIPLY && aOperation != NEOS_PY_FLOOR_DIVIDE &&
                aOperation != NEOS_PY_REMAINDER && aOperation != NEOS_PY_POWER && aOperation != NEOS_PY_TRUE_DIVIDE && aOperation != NEOS_PY_LSHIFT &&
                aOperation != NEOS_PY_RSHIFT && aOperation != NEOS_PY_AND && aOperation != NEOS_PY_OR && aOperation != NEOS_PY_XOR)
                return nullptr;
            limb_view a, b;
            if (!limbs_of(aLhs, a) || !limbs_of(aRhs, b))
                return nullptr;
            // (x + 0, x - 0 and 0 + x: the int itself (ints are immutable))
            if ((aOperation == NEOS_PY_ADD || aOperation == NEOS_PY_SUBTRACT) && b.size == 0u && aLhs->type == &sIntType.base)
                return (retain(aLhs), aLhs);
            if (aOperation == NEOS_PY_ADD && a.size == 0u && aRhs->type == &sIntType.base)
                return (retain(aRhs), aRhs);
            switch (aOperation)
            {
            case NEOS_PY_ADD: return add_views(a, b, false);
            case NEOS_PY_SUBTRACT: return add_views(a, b, true);
            case NEOS_PY_MULTIPLY: return multiply_views(a, b);
            case NEOS_PY_POWER: return b.negative || b.size > 1u ? nullptr : power_views(a, b.size == 0u ? 0u : b.data[0]);
            case NEOS_PY_TRUE_DIVIDE: return b.size == 0u ? nullptr : true_divide_views(a, b);
            // (shifts of up to 2^37 bits (and errors) as big_binary's otherwise)
            case NEOS_PY_LSHIFT: return (b.negative && b.size != 0u) || b.size > 1u || (b.size == 1u && b.data[0] >= (limb{ 1u } << 37u)) ? nullptr : 
                shift_left_views(a, b.size == 0u ? 0u : b.data[0]);
            case NEOS_PY_RSHIFT: return (b.negative && b.size != 0u) || b.size > 1u ? nullptr : shift_right_views(a, b.size == 0u ? 0u : b.data[0]);
            case NEOS_PY_AND:
            case NEOS_PY_OR:
            case NEOS_PY_XOR: return bitwise_views(aOperation, a, b);
            default: return b.size == 0u ? nullptr : divide_views(a, b, aOperation == NEOS_PY_REMAINDER);
            }
        }

        // (the order of ints (-1, 0 or 1), or 2 if they are not both ints without big integers)
        int limbs_order(object* aLhs, object* aRhs)
        {
            limb_view a, b;
            if (!limbs_of(aLhs, a) || !limbs_of(aRhs, b))
                return 2;
            bool const aNegative = a.negative && a.size != 0u;
            bool const bNegative = b.negative && b.size != 0u;
            if (aNegative != bNegative)
                return aNegative ? -1 : 1;
            auto const order = compare_limbs(a.data, a.size, b.data, b.size);
            return aNegative ? -order : order;
        }

        namespace
        {
            // (the storage of objects of one size that are made and released at a high rate (ints and floats): cells carved from 
            // blocks that are kept for reuse (as CPython's small object allocator's arenas are, apart from being returned to the 
            // system), and a list of the free cells; its users hold the GIL (see threads.cpp), so it needs no lock (nor a 
            // thread-local, which is not free to find in a plugin); its blocks are never freed (objects can be released as the 
            // process ends))
            template <std::size_t Size>
            struct object_pool
            {
                static constexpr std::size_t CellSize = (Size + alignof(std::max_align_t) - 1u) / alignof(std::max_align_t) * alignof(std::max_align_t);
                static constexpr std::size_t BlockSize = 64u * 1024u;
                void* free = nullptr;
                char* next = nullptr;
                char* end = nullptr;
                void* allocate()
                {
                    if (free != nullptr)
                    {
                        auto const result = free;
                        free = *static_cast<void**>(result);
                        return result;
                    }
                    if (next == end)
                    {
                        next = static_cast<char*>(::operator new(BlockSize));
                        end = next + BlockSize / CellSize * CellSize;
                    }
                    auto const result = next;
                    next += CellSize;
                    return result;
                }
                void deallocate(void* aCell) noexcept
                {
                    *static_cast<void**>(aCell) = free;
                    free = aCell;
                }
            };
            object_pool<sizeof(int_object)> sIntPool;
            object_pool<sizeof(float_object)> sFloatPool;
            object_pool<sizeof(str_object)> sStrPool;
            object_pool<2u * sizeof(std::uint64_t)> sLimbs2Pool;
            object_pool<4u * sizeof(std::uint64_t)> sLimbs4Pool;
            object_pool<8u * sizeof(std::uint64_t)> sLimbs8Pool;
            object_pool<16u * sizeof(std::uint64_t)> sLimbs16Pool;
        }

        std::uint64_t* allocate_limbs(std::uint32_t& aCapacity)
        {
            if (aCapacity <= 2u)
                return (aCapacity = 2u, static_cast<std::uint64_t*>(sLimbs2Pool.allocate()));
            if (aCapacity <= 4u)
                return (aCapacity = 4u, static_cast<std::uint64_t*>(sLimbs4Pool.allocate()));
            if (aCapacity <= 8u)
                return (aCapacity = 8u, static_cast<std::uint64_t*>(sLimbs8Pool.allocate()));
            if (aCapacity <= 16u)
                return (aCapacity = 16u, static_cast<std::uint64_t*>(sLimbs16Pool.allocate()));
            return new std::uint64_t[aCapacity];
        }

        void free_limbs(std::uint64_t* aLimbs, std::uint32_t aCapacity) noexcept
        {
            switch (aCapacity)
            {
            case 2u: sLimbs2Pool.deallocate(aLimbs); break;
            case 4u: sLimbs4Pool.deallocate(aLimbs); break;
            case 8u: sLimbs8Pool.deallocate(aLimbs); break;
            case 16u: sLimbs16Pool.deallocate(aLimbs); break;
            default: delete[] aLimbs; break;
            }
        }

        void* int_object::operator new(std::size_t aSize)
        {
            return aSize == sizeof(int_object) ? sIntPool.allocate() : ::operator new(aSize);
        }

        void int_object::operator delete(void* aObject, std::size_t aSize) noexcept
        {
            if (aSize == sizeof(int_object))
                sIntPool.deallocate(aObject);
            else
                ::operator delete(aObject);
        }

        void* float_object::operator new(std::size_t aSize)
        {
            return aSize == sizeof(float_object) ? sFloatPool.allocate() : ::operator new(aSize);
        }

        void float_object::operator delete(void* aObject, std::size_t aSize) noexcept
        {
            if (aSize == sizeof(float_object))
                sFloatPool.deallocate(aObject);
            else
                ::operator delete(aObject);
        }

        void* str_object::operator new(std::size_t aSize)
        {
            return aSize == sizeof(str_object) ? sStrPool.allocate() : ::operator new(aSize);
        }

        void str_object::operator delete(void* aObject, std::size_t aSize) noexcept
        {
            if (aSize == sizeof(str_object))
                sStrPool.deallocate(aObject);
            else
                ::operator delete(aObject);
        }

        // (the ints -5 to 256: the same object for each value (as CPython's); never released)
#if defined(_MSC_VER)
        __declspec(noinline)
#else
        __attribute__((noinline))
#endif
        object* const* small_ints()
        {
            static object* ints[262];
            for (long long v = -5; v <= 256; ++v)
            {
                auto* const i = new int_object{ { { &sIntType.base } }, v };
                i->h.references.store(1L << 30);
                ints[v + 5] = &i->h.base;
            }
            return ints;
        }
        object* small_int(long long aValue)
        {
            // (made once (apart: this is called often))
            static object* const* const sInts = small_ints();
            auto const result = sInts[aValue + 5];
            retain_object(result);
            return result;
        }

        big_integer const& big_of(object* aObject)
        {
            auto& i = *as<int_object>(aObject);
            // (a 128-bit integer's: made from it the first time it is needed)
            if (i.big == nullptr && i.wide)
            {
                std::array<std::uint64_t, 2u> const words{ static_cast<std::uint64_t>(i.value), static_cast<std::uint64_t>(i.high) };
                i.big = std::make_unique<big_integer>(words);
            }
            // (an int beyond 128 bits: its two's complement words, from its limbs)
            if (i.big == nullptr && i.limbs != nullptr)
            {
                std::vector<big_integer::word_t> words(i.size + 1u, 0u);
                std::copy(i.limbs, i.limbs + i.size, words.begin());
                if (i.negative)
                {
                    big_integer::word_t carry = 1u;
                    for (auto& w : words)
                    {
                        w = ~w + carry;
                        carry = (w == 0u && carry != 0u) ? 1u : 0u;
                    }
                }
                i.big = std::make_unique<big_integer>(words.begin(), words.end());
            }
            return *i.big;
        }

        // (a 64-bit integer if it fits, else a 128-bit integer if it fits)
        object* make_int(big_integer aValue)
        {
            auto const magnitude = big_detail::magnitude(aValue);
            if (magnitude == 1u)
                return make_int(static_cast<long long>(big_detail::cword(aValue, 0u)));
            if (magnitude == 2u)
                return make_int128({ static_cast<std::uint64_t>(big_detail::cword(aValue, 0u)), static_cast<std::int64_t>(big_detail::cword(aValue, 1u)) });
            // (beyond 128 bits: its limbs (its magnitude's words: its two's complement words', negated if it is negative); its big integer 
            // is kept)
            auto const size = static_cast<std::uint32_t>(magnitude);
            limb_array limbs{ size };
            bool const negative = big_negative(aValue);
            limb carry = 1u;
            for (std::uint32_t w = 0u; w < size; ++w)
            {
                auto const word = static_cast<limb>(big_detail::cword(aValue, w));
                if (negative)
                {
                    limbs[w] = ~word + carry;
                    carry = (limbs[w] == 0u && carry != 0u) ? 1u : 0u;
                }
                else
                    limbs[w] = word;
            }
            auto const result = make_limbs(std::move(limbs), size, negative);
            if (as<int_object>(result)->limbs != nullptr)
                as<int_object>(result)->big = std::make_unique<big_integer>(std::move(aValue));
            return result;
        }

        // (integers of any size (NEOS_RT_INTEGER): see the functions scripts call)
        constexpr long long sSmallIntegerLimit = 1ll << 62;

        bool integer_is_object(std::int64_t aValue)
        {
            return (aValue & 1) != 0;
        }

        object* integer_object(std::int64_t aValue)
        {
            return reinterpret_cast<object*>(static_cast<std::uintptr_t>(aValue) & ~std::uintptr_t{ 1u });
        }

        std::int64_t small_integer(long long aValue)
        {
            return static_cast<std::int64_t>(static_cast<std::uint64_t>(aValue) << 1u);
        }

        bool big_negative(big_integer const& aValue)
        {
            return big_detail::is_negative(aValue);
        }

        bool big_zero(big_integer const& aValue)
        {
            return big_detail::magnitude(aValue) == 1u && big_detail::cword(aValue, 0u) == 0u;
        }

        big_integer big_abs(big_integer const& aValue)
        {
            return big_negative(aValue) ? -aValue : aValue;
        }

        std::uint64_t big_bit_length(big_integer const& aValue)
        {
            return big_detail::magnitude_in_bits(big_abs(aValue));
        }

        // (bit aIndex of the magnitude)
        bool big_bit(big_integer const& aMagnitude, std::uint64_t aIndex)
        {
            return ((big_detail::cword(aMagnitude, static_cast<std::uint32_t>(aIndex / 64u)) >> (aIndex % 64u)) & 1u) != 0u;
        }

        // aMagnitude * 2^aExponent (the magnitude non-negative; aSticky: bits below it are set) correctly rounded to a double
        // (round half to even); false if it overflows
        bool big_to_double(big_integer const& aMagnitude, std::int64_t aExponent, bool aSticky, bool aNegative, double& aResult)
        {
            auto const bits = static_cast<std::int64_t>(big_detail::magnitude_in_bits(aMagnitude));
            if (bits == 0)
            {
                aResult = aNegative ? -0.0 : 0.0;
                return true;
            }
            auto const leading = bits - 1;
            auto const e = leading + aExponent; // (binary exponent of the leading bit)
            std::uint64_t top = 0u;
            for (std::int64_t i = 0; i < 64; ++i)
            {
                top <<= 1;
                if (leading - i >= 0 && big_bit(aMagnitude, static_cast<std::uint64_t>(leading - i)))
                    top |= 1u;
            }
            bool sticky = aSticky;
            for (std::int64_t position = leading - 64; position >= 0 && !sticky; --position)
                sticky = big_bit(aMagnitude, static_cast<std::uint64_t>(position));
            return round_to_double(top, sticky, e, aNegative, aResult);
        }

        // (a double from the top 64 bits of a magnitude (its leading bit: bit 63), whether a bit below them is set, and the binary 
        // exponent of its leading bit, correctly rounded (round half to even); false if it overflows)
        bool round_to_double(std::uint64_t top, bool sticky, std::int64_t e, bool aNegative, double& aResult)
        {
            double result = 0.0;
            if (e > 1023)
                return false;
            std::int64_t const precision = (e >= -1022 ? 53 : 53 - (-1022 - e));
            if (precision == 0)
                result = (top != (1ull << 63) || sticky) ? std::ldexp(1.0, -1074) : 0.0;
            else if (precision > 0)
            {
                auto const shift = static_cast<int>(64 - precision);
                std::uint64_t keep = top >> shift;
                std::uint64_t const rest = top & ((1ull << shift) - 1u);
                std::uint64_t const half = 1ull << (shift - 1);
                if (rest > half || (rest == half && (sticky || (keep & 1u))))
                    ++keep;
                result = std::ldexp(static_cast<double>(keep), static_cast<int>(e - (precision - 1)));
                if (std::isinf(result))
                    return false;
            }
            aResult = aNegative ? -result : result;
            return true;
        }

        bool big_to_double(big_integer const& aValue, double& aResult)
        {
            return big_to_double(big_abs(aValue), 0, false, big_negative(aValue), aResult);
        }

        // the integer part of a finite double, exactly
        big_integer big_from_double(double aValue)
        {
            int exponent = 0;
            double const m = std::frexp(std::trunc(std::fabs(aValue)), &exponent); // (|value| = m * 2^exponent, 0.5 <= m < 1)
            if (m == 0.0)
                return big_integer{ 0 };
            auto const mantissa = static_cast<long long>(std::ldexp(m, 53)); // (53 bits)
            big_integer result{ mantissa };
            if (exponent >= 53)
                result = result << static_cast<std::int64_t>(exponent - 53);
            else
                result = result >> static_cast<std::int64_t>(53 - exponent);
            return aValue < 0.0 ? -result : result;
        }

        // Python's floor division and modulo (neonumeric's truncate)
        void big_divmod(big_integer const& a, big_integer const& b, big_integer& q, big_integer& r)
        {
            q = a / b;
            r = a - q * b;
            if (!big_zero(r) && big_negative(r) != big_negative(b))
            {
                q = q - big_integer{ 1 };
                r = r + b;
            }
        }

        // sys.set_int_max_str_digits (CPython's limit on conversions between int and decimal strings)
        long long sIntMaxStrDigits = 4300;

        object* int_max_str_digits_error(std::size_t aDigits, bool aParsing)
        {
            return error("ValueError", "Exceeds the limit (" + std::to_string(sIntMaxStrDigits) + " digits) for integer string conversion" +
                (aParsing ? ": value has " + std::to_string(aDigits) + " digits" : std::string{}) + "; use sys.set_int_max_str_digits() to increase the limit");
        }

        // a big integer's digits in a base (2 to 36: lower case), without sign
        // (a magnitude's digits (lower case) in a base (2 to 36): of a power of two, its bits'; else the remainders of its divisions by the 
        // largest power of the base that is a limb)
        std::string magnitude_digits(magnitude aValue, int aBase)
        {
            static char const sDigits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
            trim(aValue);
            if (aValue.empty())
                return "0";
            std::string result;
            auto const base = static_cast<limb>(aBase);
            if ((base & (base - 1u)) == 0u)
            {
                auto const bits = static_cast<std::size_t>(std::countr_zero(base));
                auto const total = 64u * (aValue.size() - 1u) + static_cast<std::size_t>(std::bit_width(aValue.back()));
                // (its digits from the most significant: no reversal)
                auto const count = (total + bits - 1u) / bits;
                result.resize(count);
                for (std::size_t k = 0u, bit = 0u; k < count; ++k, bit += bits)
                {
                    auto const index = bit / 64u;
                    auto const offset = bit % 64u;
                    auto digit = aValue[index] >> offset;
                    if (offset + bits > 64u && index + 1u < aValue.size())
                        digit |= aValue[index + 1u] << (64u - offset);
                    result[count - 1u - k] = sDigits[digit & (base - 1u)];
                }
                return result;
            }
            else
            {
                limb chunk = base;
                std::size_t chunkDigits = 1u;
                while (chunk <= ~limb{ 0u } / base)
                {
                    chunk *= base;
                    ++chunkDigits;
                }
                magnitude quotient(aValue.size());
                while (!aValue.empty())
                {
                    auto remainder = divide_limbs(aValue.data(), static_cast<std::uint32_t>(aValue.size()), chunk, quotient.data());
                    std::copy(quotient.begin(), quotient.begin() + static_cast<std::ptrdiff_t>(aValue.size()), aValue.begin());
                    trim(aValue);
                    for (std::size_t i = 0u; i < chunkDigits && !(aValue.empty() && remainder == 0u); ++i)
                    {
                        result.push_back(sDigits[remainder % base]);
                        remainder /= base;
                    }
                }
            }
            std::reverse(result.begin(), result.end());
            return result;
        }

        std::string big_digits(big_integer const& aValue, int aBase)
        {
            if (aBase < 2 || aBase > 36)
            {
                auto digits = big_abs(aValue).to_string(static_cast<std::uint32_t>(aBase));
                for (auto& c : digits)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                return digits;
            }
            // (its magnitude: its two's complement words, negated if it is negative)
            auto const size = static_cast<std::size_t>(big_detail::magnitude(aValue));
            magnitude value(size);
            bool const negative = big_negative(aValue);
            limb carry = 1u;
            for (std::size_t w = 0u; w < size; ++w)
            {
                auto const word = static_cast<limb>(big_detail::cword(aValue, static_cast<big_integer::word_index_t>(w)));
                if (negative)
                {
                    value[w] = ~word + carry;
                    carry = (value[w] == 0u && carry != 0u) ? 1u : 0u;
                }
                else
                    value[w] = word;
            }
            return magnitude_digits(std::move(value), aBase);
        }

        // (an int's digits in a base of 2 to 36 (of its limbs: see limbs_of), and its sign; false if it has none)
        bool int_digits(object* aObject, int aBase, std::string& aDigits, bool& aNegative)
        {
            limb_view v;
            if (!limbs_of(aObject, v))
                return false;
            aDigits = magnitude_digits(magnitude{ v.data, v.data + v.size }, aBase);
            aNegative = v.negative && v.size != 0u;
            return true;
        }

        // str(int): false (ValueError) beyond the digit limit
        bool digits_str(std::string const& aDigits, bool aNegative, std::string& aResult)
        {
            if (sIntMaxStrDigits > 0 && aDigits.size() > static_cast<std::size_t>(sIntMaxStrDigits))
            {
                int_max_str_digits_error(aDigits.size(), false);
                return false;
            }
            aResult = (aNegative ? "-" : "") + aDigits;
            return true;
        }

        // str(big): false (ValueError) beyond the digit limit
        bool big_str(big_integer const& aValue, std::string& aResult)
        {
            return digits_str(big_digits(aValue, 10), big_negative(aValue), aResult);
        }

        object* big_binary(int aOperation, big_integer const& a, big_integer const& b);

        // --- conversions

        bool is_int_like(object* aObject)
        {
            auto const k = kind_of(aObject);
            return k == NEOS_PY_INT || k == NEOS_PY_BOOL;
        }

        // (an integer beyond 64 bits: OverflowError (check failed()))
        long long int_value(object* aObject)
        {
            if (is_big(aObject))
                return (error("OverflowError", "Python int too large to convert to C long"), 0);
            return kind_of(aObject) == NEOS_PY_BOOL ? (aObject == &sTrue.base ? 1 : 0) : as<int_object>(aObject)->value;
        }

        // (any int as a big integer)
        big_integer big_value(object* aObject)
        {
            return is_big(aObject) ? big_of(aObject) : big_integer{ kind_of(aObject) == NEOS_PY_BOOL ? (aObject == &sTrue.base ? 1ll : 0ll) : as<int_object>(aObject)->value };
        }

        // (any int as a big integer, without a copy of an int's big integer)
        struct big_operand
        {
            std::optional<big_integer> made;
            big_integer const* value = nullptr;
            explicit big_operand(object* aObject)
            {
                if (is_big(aObject))
                    value = &big_of(aObject);
                else
                    value = &made.emplace(kind_of(aObject) == NEOS_PY_BOOL ? (aObject == &sTrue.base ? 1ll : 0ll) : as<int_object>(aObject)->value);
            }
            big_operand(big_operand const&) = delete;
            big_integer const& operator*() const { return *value; }
        };

        bool to_index(object* aObject, long long& aResult)
        {
            if (is_big(aObject))
            {
                error("OverflowError", "cannot fit 'int' into an index-sized integer");
                return false;
            }
            if (is_int_like(aObject))
            {
                aResult = int_value(aObject);
                return true;
            }
            if (auto const type = python_type(aObject); type != nullptr && type->index != nullptr)
                return type->index(context(), aObject, &aResult) == 0;
            error("TypeError", std::string{ "'" } + type_name(aObject) + "' object cannot be interpreted as an integer");
            return false;
        }

        bool to_double(object* aObject, double& aResult)
        {
            switch (kind_of(aObject))
            {
            case NEOS_PY_BOOL:
            case NEOS_PY_INT:
                if (is_big(aObject))
                {
                    if (limb_view v; limbs_of(aObject, v))
                    {
                        if (!limbs_to_double(v.data, v.size, 0, false, v.negative && v.size != 0u, aResult))
                        {
                            error("OverflowError", "int too large to convert to float");
                            return false;
                        }
                        return true;
                    }
                    if (!big_to_double(big_of(aObject), aResult))
                    {
                        error("OverflowError", "int too large to convert to float");
                        return false;
                    }
                    return true;
                }
                aResult = static_cast<double>(int_value(aObject));
                return true;
            case NEOS_PY_FLOAT:
                aResult = as<float_object>(aObject)->value;
                return true;
            default:
                if (auto const type = python_type(aObject); type != nullptr && type->to_float != nullptr)
                    return type->to_float(context(), aObject, &aResult) == 0;
                error("TypeError", std::string{ "must be real number, not " } + type_name(aObject));
                return false;
            }
        }

        // --- repr

        std::string complex_part(double aValue)
        {
            // (as repr shows a float, without a trailing ".0")
            auto text = python_float(aValue);
            if (text.size() > 2u && text.ends_with(".0"))
                text.resize(text.size() - 2u);
            return text;
        }

        std::string str_repr(std::string_view aText)
        {
            bool const single = aText.find('\'') == std::string_view::npos || aText.find('"') != std::string_view::npos;
            char const quote = single ? '\'' : '"';
            std::string result(1u, quote);
            for (std::size_t i = 0u; i < aText.size();)
            {
                auto const c = aText[i];
                auto const b = static_cast<unsigned char>(c);
                if (b >= 0x80u)
                {
                    // (a character that is not printable (CPython's unicode_repr): its escape)
                    std::size_t const n = b < 0xE0u ? 2u : b < 0xF0u ? 3u : 4u;
                    auto const points = to_code_points(aText.substr(i, n));
                    auto const cp = points.empty() ? char32_t{ b } : points[0];
                    if (!unicode_is(cp, unicode_flag::PRINTABLE))
                    {
                        char buffer[16];
                        std::snprintf(buffer, sizeof(buffer), cp <= 0xFFu ? "\\x%02x" : cp <= 0xFFFFu ? "\\u%04x" : "\\U%08x", static_cast<unsigned>(cp));
                        result += buffer;
                    }
                    else
                        result.append(aText.substr(i, n));
                    i += n;
                    continue;
                }
                ++i;
                if (c == quote || c == '\\')
                    (result += '\\') += c;
                else if (c == '\n')
                    result += "\\n";
                else if (c == '\r')
                    result += "\\r";
                else if (c == '\t')
                    result += "\\t";
                else if (b < 0x20u || b == 0x7Fu)
                {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\x%02x", b);
                    result += buffer;
                }
                else
                    result += c;
            }
            result += quote;
            return result;
        }

        bool repr_of(object* aObject, std::string& aResult);

        bool join_repr(std::vector<object*> const& aItems, std::string& aResult)
        {
            for (std::size_t i = 0u; i < aItems.size(); ++i)
            {
                if (i != 0u)
                    aResult += ", ";
                std::string item;
                if (!repr_of(aItems[i], item))
                    return false;
                aResult += item;
            }
            return true;
        }

        bool repr_of(object* aObject, std::string& aResult)
        {
            switch (kind_of(aObject))
            {
            case NEOS_PY_NONE: aResult = "None"; return true;
            case NEOS_PY_NOT_IMPLEMENTED: aResult = "NotImplemented"; return true;
            case NEOS_PY_ELLIPSIS: aResult = "Ellipsis"; return true;
            case NEOS_PY_BOOL: aResult = aObject == &sTrue.base ? "True" : "False"; return true;
            case NEOS_PY_INT: 
                if (is_big(aObject))
                {
                    std::string digits;
                    bool negative = false;
                    if (int_digits(aObject, 10, digits, negative))
                        return digits_str(digits, negative, aResult);
                    return big_str(big_of(aObject), aResult);
                }
                aResult = std::to_string(as<int_object>(aObject)->value); 
                return true;
            case NEOS_PY_FLOAT: aResult = python_float(as<float_object>(aObject)->value); return true;
            case NEOS_PY_COMPLEX:
                {
                    auto const& c = *as<complex_object>(aObject);
                    if (c.real == 0.0 && !std::signbit(c.real))
                        aResult = complex_part(c.imag) + "j";
                    else
                        aResult = "(" + complex_part(c.real) + (c.imag < 0.0 || (c.imag == 0.0 && std::signbit(c.imag)) ? "" : "+") +
                            complex_part(c.imag) + "j)";
                }
                return true;
            case NEOS_PY_STR: aResult = str_repr(as<str_object>(aObject)->value); return true;
            case NEOS_PY_LIST:
                aResult = "[";
                if (!join_repr(as<list_object>(aObject)->items, aResult))
                    return false;
                aResult += "]";
                return true;
            case NEOS_PY_TUPLE:
                {
                    auto const& items = as<tuple_object>(aObject)->items;
                    aResult = "(";
                    if (!join_repr(items, aResult))
                        return false;
                    aResult += items.size() == 1u ? ",)" : ")";
                }
                return true;
            case NEOS_PY_DICT:
                aResult = "{";
                for (auto const& item : as<dict_object>(aObject)->items)
                {
                    if (aResult.size() > 1u)
                        aResult += ", ";
                    std::string key, value;
                    if (!repr_of(item.first, key) || !repr_of(item.second, value))
                        return false;
                    aResult += key + ": " + value;
                }
                aResult += "}";
                return true;
            case NEOS_PY_SLICE:
                {
                    auto const& s = *as<slice_object>(aObject);
                    std::string a, b, c;
                    if (!repr_of(s.start, a) || !repr_of(s.stop, b) || !repr_of(s.step, c))
                        return false;
                    aResult = "slice(" + a + ", " + b + ", " + c + ")";
                }
                return true;
            case NEOS_PY_RANGE:
                {
                    auto const& r = *as<range_object>(aObject);
                    aResult = "range(" + std::to_string(r.start) + ", " + std::to_string(r.stop) + (r.step != 1 ? ", " + std::to_string(r.step) : std::string{}) + ")";
                }
                return true;
            case NEOS_PY_ITERATOR:
                aResult = "<iterator object>";
                return true;
            case NEOS_PY_FUNCTION:
                aResult = "<built-in function " + as<function_object>(aObject)->name + ">";
                return true;
            default:
                {
                    auto const type = python_type(aObject);
                    if (type == nullptr || type->repr == nullptr)
                    {
                        aResult = std::string{ "<" } + type_name(aObject) + " object>";
                        return true;
                    }
                    ref const text{ type->repr(context(), aObject) };
                    if (!text)
                        return false;
                    if (kind_of(text.get()) != NEOS_PY_STR)
                    {
                        error("TypeError", std::string{ "__repr__ returned non-string (type " } + type_name(text.get()) + ")");
                        return false;
                    }
                    aResult = as<str_object>(text.get())->value;
                }
                return true;
            }
        }

        bool str_of(object* aObject, std::string& aResult)
        {
            if (kind_of(aObject) == NEOS_PY_STR)
            {
                aResult = as<str_object>(aObject)->value;
                return true;
            }
            if (auto const type = python_type(aObject); type != nullptr && type->str != nullptr && kind_of(aObject) == NEOS_PY_OTHER)
            {
                ref const text{ type->str(context(), aObject) };
                if (!text)
                    return false;
                if (kind_of(text.get()) != NEOS_PY_STR)
                {
                    error("TypeError", std::string{ "__str__ returned non-string (type " } + type_name(text.get()) + ")");
                    return false;
                }
                aResult = as<str_object>(text.get())->value;
                return true;
            }
            return repr_of(aObject, aResult);
        }

        // --- operations

        object* binary(int aOperation, object* aLhs, object* aRhs, bool aInplace);
        object* compare(int aOperation, object* aLhs, object* aRhs);
        int truth(object* aObject);

        char const* operator_symbol(int aOperation, bool aInplace = false)
        {
            static char const* const sSymbols[] = { "+", "-", "*", "/", "//", "%", "**", "@", "<<", ">>", "&", "|", "^" };
            static char const* const sInplaceSymbols[] = { "+=", "-=", "*=", "/=", "//=", "%=", "**=", "@=", "<<=", ">>=", "&=", "|=", "^=" };
            return aOperation >= 0 && aOperation <= NEOS_PY_XOR ? (aInplace ? sInplaceSymbols[aOperation] : sSymbols[aOperation]) : "?";
        }

        char const* comparison_symbol(int aOperation)
        {
            static char const* const sSymbols[] = { "<", "<=", "==", "!=", ">", ">=" };
            return aOperation >= 0 && aOperation <= NEOS_PY_GE ? sSymbols[aOperation] : "?";
        }

        // Python's floor division and modulo of floats (CPython's float_divmod)
        std::pair<double, double> float_divmod(double aVx, double aWx)
        {
            double mod = std::fmod(aVx, aWx);
            double div = (aVx - mod) / aWx;
            if (mod != 0.0)
            {
                if ((aWx < 0.0) != (mod < 0.0))
                {
                    mod += aWx;
                    div -= 1.0;
                }
            }
            else
                mod = std::copysign(0.0, aWx);
            double floordiv;
            if (div != 0.0)
            {
                floordiv = std::floor(div);
                if (div - floordiv > 0.5)
                    floordiv += 1.0;
            }
            else
                floordiv = std::copysign(0.0, aVx / aWx);
            return { floordiv, mod };
        }

        object* int_overflow()
        {
            return error("OverflowError", "integer result too large (neos Python ints are 64-bit)");
        }

        // (portable overflow checking: true if the result overflows)
        bool add_overflow(long long a, long long b, long long* r)
        {
            if ((b > 0 && a > std::numeric_limits<long long>::max() - b) || (b < 0 && a < std::numeric_limits<long long>::min() - b))
                return true;
            *r = a + b;
            return false;
        }
        bool subtract_overflow(long long a, long long b, long long* r)
        {
            if ((b < 0 && a > std::numeric_limits<long long>::max() + b) || (b > 0 && a < std::numeric_limits<long long>::min() + b))
                return true;
            *r = a - b;
            return false;
        }
        bool multiply_overflow(long long a, long long b, long long* r)
        {
            if (a == 0 || b == 0)
            {
                *r = 0;
                return false;
            }
            auto const max = std::numeric_limits<long long>::max();
            auto const min = std::numeric_limits<long long>::min();
            if ((a == -1 && b == min) || (b == -1 && a == min))
                return true;
            if (a > 0 ? (b > 0 ? a > max / b : b < min / a) : (b > 0 ? a < min / b : a < max / b))
                return true;
            *r = a * b;
            return false;
        }

        object* int_binary(int aOperation, long long a, long long b)
        {
            long long r = 0;
            switch (aOperation)
            {
            case NEOS_PY_ADD:
                if (add_overflow(a, b, &r))
                {
                    // (a 128-bit integer)
                    int128 sum;
                    add128(int128_of(a), int128_of(b), sum);
                    return make_int128(sum);
                }
                return make_int(r);
            case NEOS_PY_SUBTRACT:
                if (subtract_overflow(a, b, &r))
                {
                    int128 difference;
                    subtract128(int128_of(a), int128_of(b), difference);
                    return make_int128(difference);
                }
                return make_int(r);
            case NEOS_PY_MULTIPLY:
                if (multiply_overflow(a, b, &r)) return make_int128(multiply64(a, b));
                return make_int(r);
            case NEOS_PY_TRUE_DIVIDE:
                if (b == 0) return error("ZeroDivisionError", "division by zero");
                // (exact as doubles: correctly rounded; else as CPython, from the integers)
                if (a > (1ll << 53) || a < -(1ll << 53) || b > (1ll << 53) || b < -(1ll << 53))
                {
                    limb_view x, y;
                    view_of(int128_of(a), x);
                    view_of(int128_of(b), y);
                    return true_divide_views(x, y);
                }
                return make_float(static_cast<double>(a) / static_cast<double>(b));
            case NEOS_PY_FLOOR_DIVIDE:
            case NEOS_PY_REMAINDER:
                {
                    if (b == 0)
                        return error("ZeroDivisionError", aOperation == NEOS_PY_FLOOR_DIVIDE ? "integer division or modulo by zero" : "integer modulo by zero");
                    if (a == std::numeric_limits<long long>::min() && b == -1)
                        return aOperation == NEOS_PY_FLOOR_DIVIDE ? big_binary(aOperation, big_integer{ a }, big_integer{ b }) : make_int(0);
                    long long q = a / b;
                    long long m = a % b;
                    if (m != 0 && ((m < 0) != (b < 0)))
                    {
                        m += b;
                        --q;
                    }
                    return make_int(aOperation == NEOS_PY_FLOOR_DIVIDE ? q : m);
                }
            case NEOS_PY_POWER:
                {
                    if (b < 0)
                    {
                        if (a == 0)
                            return error("ZeroDivisionError", "0.0 cannot be raised to a negative power");
                        return make_float(std::pow(static_cast<double>(a), static_cast<double>(b)));
                    }
                    long long result = 1;
                    long long base = a;
                    // (beyond 64 bits: with limbs)
                    auto const wider = [&]()
                    {
                        limb_view view;
                        view_of(int128_of(a), view);
                        return power_views(view, static_cast<limb>(b));
                    };
                    for (long long e = b; e > 0; e >>= 1)
                    {
                        if ((e & 1) != 0 && multiply_overflow(result, base, &result))
                            return wider();
                        if (e > 1 && multiply_overflow(base, base, &base))
                            return wider();
                    }
                    return make_int(result);
                }
            case NEOS_PY_LSHIFT:
                if (b < 0) return error("ValueError", "negative shift count");
                if (a == 0) return make_int(0);
                if (b >= 63 || (a > 0 ? a > (std::numeric_limits<long long>::max() >> b) : a < (std::numeric_limits<long long>::min() >> b)))
                {
                    if (b >= (1ll << 37))
                        return big_binary(aOperation, big_integer{ a }, big_integer{ b });
                    limb_view view;
                    view_of(int128_of(a), view);
                    return shift_left_views(view, static_cast<std::uint64_t>(b));
                }
                return make_int(static_cast<long long>(static_cast<unsigned long long>(a) << b));
            case NEOS_PY_RSHIFT:
                if (b < 0) return error("ValueError", "negative shift count");
                return make_int(b >= 63 ? (a < 0 ? -1 : 0) : a >> b);
            case NEOS_PY_AND: return make_int(a & b);
            case NEOS_PY_OR: return make_int(a | b);
            case NEOS_PY_XOR: return make_int(a ^ b);
            default:
                return not_implemented();
            }
        }

        // the operations on integers beyond 64 bits (as CPython's Objects/longobject.c)
        object* big_binary(int aOperation, big_integer const& a, big_integer const& b)
        {
            switch (aOperation)
            {
            case NEOS_PY_ADD: return make_int(a + b);
            case NEOS_PY_SUBTRACT: return make_int(a - b);
            case NEOS_PY_MULTIPLY: return make_int(a * b);
            case NEOS_PY_TRUE_DIVIDE:
                {
                    if (big_zero(b))
                        return error("ZeroDivisionError", "division by zero");
                    bool const negative = big_negative(a) != big_negative(b);
                    if (big_zero(a))
                        return make_float(negative ? -0.0 : 0.0);
                    auto const x = big_abs(a);
                    auto const y = big_abs(b);
                    auto const shift = static_cast<std::int64_t>(big_detail::magnitude_in_bits(x)) - static_cast<std::int64_t>(big_detail::magnitude_in_bits(y)) - 55;
                    if (shift > 1100)
                        return error("OverflowError", "integer division result too large for a float");
                    if (shift < -1200)
                        return make_float(negative ? -0.0 : 0.0);
                    // (x / y = q * 2^shift (+ the remainder): q has 55 or 56 bits)
                    big_integer const n = shift < 0 ? x << -shift : x;
                    big_integer const d = shift > 0 ? y << shift : y;
                    big_integer const q = n / d;
                    big_integer const r = n - q * d;
                    double result = 0.0;
                    if (!big_to_double(q, shift, !big_zero(r), negative, result))
                        return error("OverflowError", "integer division result too large for a float");
                    return make_float(result);
                }
            case NEOS_PY_FLOOR_DIVIDE:
            case NEOS_PY_REMAINDER:
                {
                    if (big_zero(b))
                        return error("ZeroDivisionError", aOperation == NEOS_PY_FLOOR_DIVIDE ? "integer division or modulo by zero" : "integer modulo by zero");
                    big_integer q, r;
                    big_divmod(a, b, q, r);
                    return make_int(aOperation == NEOS_PY_FLOOR_DIVIDE ? q : r);
                }
            case NEOS_PY_POWER:
                {
                    if (big_negative(b))
                    {
                        if (big_zero(a))
                            return error("ZeroDivisionError", "0.0 cannot be raised to a negative power");
                        double x = 0.0, y = 0.0;
                        if (!big_to_double(a, x) || !big_to_double(b, y))
                            return error("OverflowError", "int too large to convert to float");
                        return make_float(std::pow(x, y));
                    }
                    if (big_detail::magnitude(b) > 1u)
                    {
                        // (an exponent beyond 64 bits: only 0, 1 and -1 have a result that fits in memory)
                        if (big_zero(a) || a == big_integer{ 1 })
                            return make_int(a);
                        if (a == big_integer{ -1 })
                            return make_int(big_bit(b, 0u) ? -1ll : 1ll);
                        return error("MemoryError", "");
                    }
                    auto e = static_cast<std::uint64_t>(big_detail::cword(b, 0u));
                    big_integer result{ 1 };
                    big_integer base = a;
                    for (; e != 0u; e >>= 1)
                    {
                        if ((e & 1u) != 0u)
                            result = result * base;
                        if (e > 1u)
                            base = base * base;
                    }
                    return make_int(std::move(result));
                }
            case NEOS_PY_LSHIFT:
                {
                    if (big_negative(b))
                        return error("ValueError", "negative shift count");
                    if (big_zero(a))
                        return make_int(0);
                    if (big_detail::magnitude(b) > 1u)
                        return error("OverflowError", "too many digits in integer");
                    auto n = static_cast<std::uint64_t>(big_detail::cword(b, 0u));
                    if (n > (std::uint64_t{ 1 } << 40))
                        return error("OverflowError", "too many digits in integer");
                    big_integer result = a;
                    // (neonumeric shifts at most 0xFFFF00 bits at once)
                    for (std::uint64_t constexpr chunk = 0x800000u; n > 0u; n -= std::min(n, chunk))
                        result = result << static_cast<std::int64_t>(std::min(n, chunk));
                    return make_int(std::move(result));
                }
            case NEOS_PY_RSHIFT:
                {
                    if (big_negative(b))
                        return error("ValueError", "negative shift count");
                    auto const bits = big_bit_length(a);
                    if (big_detail::magnitude(b) > 1u || static_cast<std::uint64_t>(big_detail::cword(b, 0u)) > bits)
                        return make_int(big_negative(a) ? -1ll : 0ll);
                    return make_int(a >> static_cast<std::int64_t>(big_detail::cword(b, 0u)));
                }
            case NEOS_PY_AND: return make_int(a & b);
            case NEOS_PY_OR: return make_int(a | b);
            case NEOS_PY_XOR: return make_int(a ^ b);
            default:
                return not_implemented();
            }
        }

        // complex arithmetic as CPython does it (Objects/complexobject.c)
        struct cpx { double real; double imag; };
        cpx c_prod(cpx a, cpx b) { return { a.real * b.real - a.imag * b.imag, a.real * b.imag + a.imag * b.real }; }
        bool c_quot(cpx a, cpx b, cpx& r)
        {
            double const abs_breal = b.real < 0 ? -b.real : b.real;
            double const abs_bimag = b.imag < 0 ? -b.imag : b.imag;
            if (abs_breal >= abs_bimag)
            {
                if (abs_breal == 0.0)
                    return false;
                double const ratio = b.imag / b.real;
                double const denom = b.real + b.imag * ratio;
                r = { (a.real + a.imag * ratio) / denom, (a.imag - a.real * ratio) / denom };
            }
            else if (abs_bimag >= abs_breal)
            {
                double const ratio = b.real / b.imag;
                double const denom = b.real * ratio + b.imag;
                r = { (a.real * ratio + a.imag) / denom, (a.imag * ratio - a.real) / denom };
            }
            else
                r = { std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN() };
            return true;
        }
        cpx c_powu(cpx x, long n)
        {
            cpx r{ 1.0, 0.0 }, p = x;
            for (long mask = 1; mask > 0 && n >= mask; mask <<= 1)
            {
                if (n & mask)
                    r = c_prod(r, p);
                p = c_prod(p, p);
            }
            return r;
        }
        bool c_pow(cpx a, cpx b, cpx& r)
        {
            if (b.real == 0.0 && b.imag == 0.0)
                r = { 1.0, 0.0 };
            else if (a.real == 0.0 && a.imag == 0.0)
            {
                if (b.imag != 0.0 || b.real < 0.0)
                    return false;
                r = { 0.0, 0.0 };
            }
            else if (b.imag == 0.0 && b.real == std::floor(b.real) && std::fabs(b.real) <= 100.0)
            {
                // (c_powi)
                auto const n = static_cast<long>(b.real);
                if (n > 0)
                    r = c_powu(a, n);
                else
                    c_quot({ 1.0, 0.0 }, c_powu(a, -n), r);
            }
            else
            {
                double const vabs = std::hypot(a.real, a.imag);
                double len = std::pow(vabs, b.real);
                double const at = std::atan2(a.imag, a.real);
                double phase = at * b.real;
                if (b.imag != 0.0)
                {
                    len /= std::exp(at * b.imag);
                    phase += b.imag * std::log(vabs);
                }
                r = { len * std::cos(phase), len * std::sin(phase) };
            }
            return true;
        }

        object* complex_binary(int aOperation, std::complex<double> a, std::complex<double> b)
        {
            cpx const x{ a.real(), a.imag() }, y{ b.real(), b.imag() };
            switch (aOperation)
            {
            case NEOS_PY_ADD: return make_complex(x.real + y.real, x.imag + y.imag);
            case NEOS_PY_SUBTRACT: return make_complex(x.real - y.real, x.imag - y.imag);
            case NEOS_PY_MULTIPLY: { auto const r = c_prod(x, y); return make_complex(r.real, r.imag); }
            case NEOS_PY_TRUE_DIVIDE:
                {
                    cpx r{};
                    if (!c_quot(x, y, r))
                        return error("ZeroDivisionError", "division by zero");
                    return make_complex(r.real, r.imag);
                }
            case NEOS_PY_POWER:
                {
                    cpx r{};
                    if (!c_pow(x, y, r))
                        return error("ZeroDivisionError", "zero to a negative or complex power");
                    return make_complex(r.real, r.imag);
                }
            default:
                return not_implemented();
            }
        }

        object* float_binary(int aOperation, double a, double b)
        {
            switch (aOperation)
            {
            case NEOS_PY_ADD: return make_float(a + b);
            case NEOS_PY_SUBTRACT: return make_float(a - b);
            case NEOS_PY_MULTIPLY: return make_float(a * b);
            case NEOS_PY_TRUE_DIVIDE:
                if (b == 0.0) return error("ZeroDivisionError", "float division by zero");
                return make_float(a / b);
            case NEOS_PY_FLOOR_DIVIDE:
                if (b == 0.0) return error("ZeroDivisionError", "float floor division by zero");
                return make_float(float_divmod(a, b).first);
            case NEOS_PY_REMAINDER:
                if (b == 0.0) return error("ZeroDivisionError", "float modulo by zero");
                return make_float(float_divmod(a, b).second);
            case NEOS_PY_POWER:
                if (a == 0.0 && b < 0.0)
                    return error("ZeroDivisionError", "0.0 cannot be raised to a negative power");
                if (a < 0.0 && std::isfinite(b) && std::floor(b) != b)
                    return complex_binary(NEOS_PY_POWER, { a, 0.0 }, { b, 0.0 });
                return make_float(std::pow(a, b));
            default:
                return not_implemented();
            }
        }

        bool sequence_repeat(std::vector<object*> const& aItems, object* aCount, std::vector<object*>& aResult)
        {
            long long count = 0;
            if (!is_int_like(aCount))
                return false;
            if (is_big(aCount) && !big_negative(big_of(aCount)))
            {
                error("OverflowError", "cannot fit 'int' into an index-sized integer");
                return true;
            }
            count = is_big(aCount) ? 0 : std::max(0ll, int_value(aCount));
            if (count == 0 || aItems.empty())
                return true;
            if (static_cast<unsigned long long>(count) > aResult.max_size() / aItems.size())
            {
                error("MemoryError", "");
                return true;
            }
            // (each item's references: added at once (as CPython's), if they are counted in its header)
            for (auto item : aItems)
                if (item != nullptr && item->type->retain == &retain_object)
                {
                    auto& h = *reinterpret_cast<header*>(item);
                    h.references.store(h.references.load(std::memory_order_relaxed) + static_cast<long>(count), std::memory_order_relaxed);
                }
                else
                    for (long long i = 0; i < count; ++i)
                        retain(item);
            aResult.reserve(aResult.size() + static_cast<std::size_t>(count) * aItems.size());
            if (aItems.size() == 1u)
                aResult.insert(aResult.end(), static_cast<std::size_t>(count), aItems[0]);
            else
                for (long long i = 0; i < count; ++i)
                    aResult.insert(aResult.end(), aItems.begin(), aItems.end());
            return true;
        }

        // the core types' binary operators (NotImplemented if not supported)
        // (user classes: see below)
        std::map<std::string, std::vector<std::string>, std::less<>>& user_exception_bases();
        struct attribute_dict;
        object* user_getattr(object* aSelf, attribute_dict& aAttributes, char const* aName);
        int user_setattr(object* aSelf, attribute_dict& aAttributes, char const* aName, object* aValue);
        object* special_method(object* aObject, char const* aName);
        object* user_class_of(object* aObject);
        bool is_class(object* aObject);
        bool is_subclass(object* aClass, object* aBase);
        std::string const& as_class_name(object* aClass);
        object* percent_format(std::string const& aFormat, object* aValues);
        void raise_key_error(object* aKey);

        object* core_binary(int aOperation, object* aLhs, object* aRhs)
        {
            auto const lk = kind_of(aLhs);
            auto const rk = kind_of(aRhs);
            bool const lnum = (lk == NEOS_PY_BOOL || lk == NEOS_PY_INT || lk == NEOS_PY_FLOAT || lk == NEOS_PY_COMPLEX);
            bool const rnum = (rk == NEOS_PY_BOOL || rk == NEOS_PY_INT || rk == NEOS_PY_FLOAT || rk == NEOS_PY_COMPLEX);
            if (lnum && rnum)
            {
                if (lk == NEOS_PY_COMPLEX || rk == NEOS_PY_COMPLEX)
                {
                    auto const value = [](object* aObject, int aKind) -> std::complex<double>
                    {
                        if (aKind == NEOS_PY_COMPLEX)
                            return { as<complex_object>(aObject)->real, as<complex_object>(aObject)->imag };
                        double x = 0.0;
                        to_double(aObject, x);
                        return { x, 0.0 };
                    };
                    if (aOperation == NEOS_PY_FLOOR_DIVIDE || aOperation == NEOS_PY_REMAINDER)
                        return error("TypeError", std::string{ "unsupported operand type(s) for " } + operator_symbol(aOperation) + ": '" + type_name(aLhs) + "' and '" + type_name(aRhs) + "'");
                    auto const l = value(aLhs, lk);
                    auto const r = value(aRhs, rk);
                    if (failed())
                        return nullptr;
                    return complex_binary(aOperation, l, r);
                }
                if (lk == NEOS_PY_FLOAT || rk == NEOS_PY_FLOAT)
                {
                    if (aOperation >= NEOS_PY_MATMUL)
                        return not_implemented();
                    double a = 0.0, b = 0.0;
                    if (!to_double(aLhs, a) || !to_double(aRhs, b))
                        return nullptr;
                    return float_binary(aOperation, a, b);
                }
                if (lk == NEOS_PY_BOOL && rk == NEOS_PY_BOOL && (aOperation == NEOS_PY_AND || aOperation == NEOS_PY_OR || aOperation == NEOS_PY_XOR))
                {
                    bool const a = aLhs == &sTrue.base, b = aRhs == &sTrue.base;
                    return make_bool(aOperation == NEOS_PY_AND ? a && b : aOperation == NEOS_PY_OR ? a || b : a != b);
                }
                if (is_big(aLhs) || is_big(aRhs))
                {
                    if (auto const result = int128_binary(aOperation, aLhs, aRhs))
                        return result;
                    if (auto const result = limbs_binary(aOperation, aLhs, aRhs))
                        return result;
                    return big_binary(aOperation, *big_operand{ aLhs }, *big_operand{ aRhs });
                }
                return int_binary(aOperation, int_value(aLhs), int_value(aRhs));
            }
            if (aOperation == NEOS_PY_REMAINDER && lk == NEOS_PY_STR)
                return percent_format(as<str_object>(aLhs)->value, aRhs);
            if (aOperation == NEOS_PY_ADD)
            {
                if (lk == NEOS_PY_STR && rk == NEOS_PY_STR)
                    return make_str(as<str_object>(aLhs)->value + as<str_object>(aRhs)->value);
                if (lk == NEOS_PY_STR && rk != NEOS_PY_OTHER)
                    return error("TypeError", std::string{ "can only concatenate str (not \"" } + type_name(aRhs) + "\") to str");
                if ((lk == NEOS_PY_LIST && rk == NEOS_PY_LIST) || (lk == NEOS_PY_TUPLE && rk == NEOS_PY_TUPLE))
                {
                    auto const& a = lk == NEOS_PY_LIST ? as<list_object>(aLhs)->items : as<tuple_object>(aLhs)->items;
                    auto const& b = lk == NEOS_PY_LIST ? as<list_object>(aRhs)->items : as<tuple_object>(aRhs)->items;
                    std::vector<object*> items;
                    for (auto i : a) { retain(i); items.push_back(i); }
                    for (auto i : b) { retain(i); items.push_back(i); }
                    return lk == NEOS_PY_LIST ? make_list(std::move(items)) : make_tuple(std::move(items));
                }
                if (lk == NEOS_PY_LIST && rk != NEOS_PY_OTHER)
                    return error("TypeError", std::string{ "can only concatenate list (not \"" } + type_name(aRhs) + "\") to list");
                if (lk == NEOS_PY_TUPLE && rk != NEOS_PY_OTHER)
                    return error("TypeError", std::string{ "can only concatenate tuple (not \"" } + type_name(aRhs) + "\") to tuple");
            }
            if (aOperation == NEOS_PY_MULTIPLY)
            {
                auto const repeat = [&](object* aSequence, int aKind, object* aCount) -> object*
                {
                    if (aKind == NEOS_PY_STR && is_int_like(aCount))
                    {
                        if (is_big(aCount))
                            return big_negative(big_of(aCount)) ? make_str("") : error("OverflowError", "cannot fit 'int' into an index-sized integer");
                        std::string result;
                        for (long long i = 0; i < int_value(aCount); ++i)
                            result += as<str_object>(aSequence)->value;
                        return make_str(std::move(result));
                    }
                    std::vector<object*> items;
                    if (aKind == NEOS_PY_LIST && sequence_repeat(as<list_object>(aSequence)->items, aCount, items))
                        return failed() ? nullptr : make_list(std::move(items));
                    if (aKind == NEOS_PY_TUPLE && sequence_repeat(as<tuple_object>(aSequence)->items, aCount, items))
                        return failed() ? nullptr : make_tuple(std::move(items));
                    return nullptr;
                };
                if (auto const result = repeat(aLhs, lk, aRhs))
                    return result;
                if (failed())
                    return nullptr;
                if (auto const result = repeat(aRhs, rk, aLhs))
                    return result;
                if (failed())
                    return nullptr;
            }
            return not_implemented();
        }

        object* binary(int aOperation, object* aLhs, object* aRhs, bool aInplace)
        {
            // (ints that are 128-bit integers: native (no dispatch))
            if (aLhs != nullptr && aRhs != nullptr && aLhs->type == &sIntType.base && aRhs->type == &sIntType.base)
            {
                if (auto const result = int128_binary(aOperation, aLhs, aRhs))
                    return result;
                // (an int beyond 64 bits: native (its limbs), else its big integer's (as core_binary's))
                if (is_big(aLhs) || is_big(aRhs))
                {
                    if (auto const result = limbs_binary(aOperation, aLhs, aRhs))
                        return result;
                    return big_binary(aOperation, *big_operand{ aLhs }, *big_operand{ aRhs });
                }
            }
            // (floats' +, - and *: native (no dispatch))
            if (aLhs != nullptr && aRhs != nullptr && aLhs->type == &sFloatType.base && aRhs->type == &sFloatType.base &&
                (aOperation == NEOS_PY_ADD || aOperation == NEOS_PY_SUBTRACT || aOperation == NEOS_PY_MULTIPLY))
            {
                auto const a = as<float_object>(aLhs)->value;
                auto const b = as<float_object>(aRhs)->value;
                return make_float(aOperation == NEOS_PY_ADD ? a + b : aOperation == NEOS_PY_SUBTRACT ? a - b : a * b);
            }
            auto const lt = python_type(aLhs);
            auto const rt = python_type(aRhs);
            if (aInplace && lt != nullptr && lt->binary != nullptr && kind_of(aLhs) == NEOS_PY_OTHER)
            {
                ref result{ lt->binary(context(), aOperation, aLhs, aRhs, 1) };
                if (!result || kind_of(result.get()) != NEOS_PY_NOT_IMPLEMENTED)
                    return result.release_ownership();
            }
            // (list += and *=, and dict |=: in place; dict | dict)
            if (aInplace && kind_of(aLhs) == NEOS_PY_LIST && aOperation == NEOS_PY_ADD)
            {
                std::vector<object*> items;
                if (!collect(aRhs, items))
                    return nullptr;
                auto& target = as<list_object>(aLhs)->items;
                target.insert(target.end(), items.begin(), items.end());
                return (retain(aLhs), aLhs);
            }
            if (aInplace && kind_of(aLhs) == NEOS_PY_LIST && aOperation == NEOS_PY_MULTIPLY && is_int_like(builtin_value(aRhs)))
            {
                long long n = 0;
                if (!to_index(aRhs, n))
                    return nullptr;
                auto& target = as<list_object>(aLhs)->items;
                std::vector<object*> const original = target;
                if (n <= 0)
                {
                    target.clear();
                    for (auto i : original)
                        release(i);
                }
                else
                    for (long long r = 1; r < n; ++r)
                        for (auto i : original)
                            target.push_back((retain(i), i));
                return (retain(aLhs), aLhs);
            }
            if (kind_of(aLhs) == NEOS_PY_DICT && aOperation == NEOS_PY_OR && (aInplace || kind_of(builtin_value(aRhs)) == NEOS_PY_DICT))
            {
                ref result{ aInplace ? (retain(aLhs), aLhs) : make_dict() };
                if ((!aInplace && !dict_update(result.get(), aLhs)) || !dict_update(result.get(), aRhs))
                    return nullptr;
                return result.release_ownership();
            }
            if (kind_of(aLhs) != NEOS_PY_OTHER)
            {
                ref result{ core_binary(aOperation, aLhs, aRhs) };
                if (!result || kind_of(result.get()) != NEOS_PY_NOT_IMPLEMENTED)
                    return result.release_ownership();
            }
            else if (lt != nullptr && lt->binary != nullptr)
            {
                ref result{ lt->binary(context(), aOperation, aLhs, aRhs, 0) };
                if (!result || kind_of(result.get()) != NEOS_PY_NOT_IMPLEMENTED)
                    return result.release_ownership();
            }
            if (kind_of(aRhs) == NEOS_PY_OTHER && rt != nullptr && rt->binary != nullptr && rt != lt)
            {
                ref result{ rt->binary(context(), aOperation, aLhs, aRhs, 0) };
                if (!result || kind_of(result.get()) != NEOS_PY_NOT_IMPLEMENTED)
                    return result.release_ownership();
            }
            // (X | Y of types: a union)
            if (aOperation == NEOS_PY_OR && is_union_operand(aLhs) && is_union_operand(aRhs) && (kind_of(aLhs) != NEOS_PY_NONE || kind_of(aRhs) != NEOS_PY_NONE))
                return make_union(aLhs, aRhs);
            return error("TypeError", std::string{ "unsupported operand type(s) for " } + operator_symbol(aOperation, aInplace) + ": '" + type_name(aLhs) + "' and '" + type_name(aRhs) + "'");
        }

        object* unary(int aOperation, object* aObject)
        {
            switch (kind_of(aObject))
            {
            case NEOS_PY_BOOL:
            case NEOS_PY_INT:
                {
                    // (an int beyond 64 bits: its limbs, its sign changed (~x: -x - 1))
                    if (limb_view v; kind_of(aObject) == NEOS_PY_INT && is_big(aObject) && limbs_of(aObject, v))
                    {
                        if (aOperation == NEOS_PY_INVERT)
                        {
                            limb_view one;
                            view_of(int128_of(1), one);
                            v.negative = !v.negative;
                            return add_views(v, one, true);
                        }
                        limb_array limbs{ std::max(v.size, 1u) };
                        std::copy(v.data, v.data + v.size, limbs.get());
                        bool const negative = aOperation == NEOS_PY_NEGATIVE ? !v.negative : aOperation == NEOS_PY_ABSOLUTE ? false : v.negative;
                        return make_limbs(std::move(limbs), v.size, negative);
                    }
                    if (is_big(aObject))
                    {
                        auto const& v = big_of(aObject);
                        switch (aOperation)
                        {
                        case NEOS_PY_NEGATIVE: return make_int(-v);
                        case NEOS_PY_POSITIVE: return make_int(v);
                        case NEOS_PY_INVERT: return make_int(~v);
                        case NEOS_PY_ABSOLUTE: return make_int(big_abs(v));
                        }
                        break;
                    }
                    auto const v = int_value(aObject);
                    switch (aOperation)
                    {
                    case NEOS_PY_NEGATIVE: if (v == std::numeric_limits<long long>::min()) return make_int(-big_integer{ v }); return make_int(-v);
                    case NEOS_PY_POSITIVE: return make_int(v);
                    case NEOS_PY_INVERT: return make_int(~v);
                    case NEOS_PY_ABSOLUTE: if (v == std::numeric_limits<long long>::min()) return make_int(-big_integer{ v }); return make_int(v < 0 ? -v : v);
                    }
                }
                break;
            case NEOS_PY_FLOAT:
                {
                    auto const v = as<float_object>(aObject)->value;
                    switch (aOperation)
                    {
                    case NEOS_PY_NEGATIVE: return make_float(-v);
                    case NEOS_PY_POSITIVE: return make_float(v);
                    case NEOS_PY_ABSOLUTE: return make_float(std::fabs(v));
                    }
                }
                break;
            case NEOS_PY_COMPLEX:
                {
                    auto const& c = *as<complex_object>(aObject);
                    switch (aOperation)
                    {
                    case NEOS_PY_NEGATIVE: return make_complex(-c.real, -c.imag);
                    case NEOS_PY_POSITIVE: return make_complex(c.real, c.imag);
                    case NEOS_PY_ABSOLUTE: return make_float(std::hypot(c.real, c.imag));
                    }
                }
                break;
            case NEOS_PY_OTHER:
                if (auto const type = python_type(aObject); type != nullptr && type->unary != nullptr)
                    return type->unary(context(), aOperation, aObject);
                break;
            default:
                break;
            }
            if (aOperation == NEOS_PY_ABSOLUTE)
                return error("TypeError", std::string{ "bad operand type for abs(): '" } + type_name(aObject) + "'");
            static char const* const sSymbols[] = { "-", "+", "~", "abs()" };
            return error("TypeError", std::string{ "bad operand type for unary " } + sSymbols[std::clamp(aOperation, 0, 3)] + ": '" + type_name(aObject) + "'");
        }

        // three way comparison of core objects: -1, 0, 1; 2 if unordered (NaN); 3 if not comparable
        int core_order(object* aLhs, object* aRhs, int& aError, bool aEquality = false);

        // (aEquality: only whether they are equal is needed (4: not equal): their first unequal items are not ordered)
        int sequence_order(std::vector<object*> const& a, std::vector<object*> const& b, int& aError, bool aEquality)
        {
            for (std::size_t i = 0u; i < std::min(a.size(), b.size()); ++i)
            {
                if (a[i] == b[i])
                    continue;   // (identity: equal (as CPython's containers compare their items))
                ref const equal{ compare(NEOS_PY_EQ, a[i], b[i]) };
                if (!equal) { aError = 1; return 0; }
                auto const t = truth(equal.get());
                if (t < 0) { aError = 1; return 0; }
                if (t == 0)
                {
                    if (aEquality)
                        return 4;
                    ref const less{ compare(NEOS_PY_LT, a[i], b[i]) };
                    if (!less) { aError = 1; return 0; }
                    auto const l = truth(less.get());
                    if (l < 0) { aError = 1; return 0; }
                    return l != 0 ? -1 : 1;
                }
            }
            return a.size() < b.size() ? -1 : a.size() > b.size() ? 1 : 0;
        }

        long long dict_find(dict_object& aDict, object* aKey, long long aHash);

        int core_order(object* aLhs, object* aRhs, int& aError, bool aEquality)
        {
            auto const lk = kind_of(aLhs);
            auto const rk = kind_of(aRhs);
            bool const lnum = (lk == NEOS_PY_BOOL || lk == NEOS_PY_INT || lk == NEOS_PY_FLOAT);
            bool const rnum = (rk == NEOS_PY_BOOL || rk == NEOS_PY_INT || rk == NEOS_PY_FLOAT);
            if (lnum && rnum)
            {
                if (lk != NEOS_PY_FLOAT && rk != NEOS_PY_FLOAT)
                {
                    if (is_big(aLhs) || is_big(aRhs))
                    {
                        int128 a128, b128;
                        if (int128_value(aLhs, a128) && int128_value(aRhs, b128))
                            return compare128(a128, b128);
                        if (auto const order = limbs_order(aLhs, aRhs); order != 2)
                            return order;
                        big_operand const a{ aLhs }, b{ aRhs };
                        return *a < *b ? -1 : *b < *a ? 1 : 0;
                    }
                    auto const a = int_value(aLhs), b = int_value(aRhs);
                    return a < b ? -1 : a > b ? 1 : 0;
                }
                if (lk == NEOS_PY_FLOAT && rk == NEOS_PY_FLOAT)
                {
                    auto const a = as<float_object>(aLhs)->value, b = as<float_object>(aRhs)->value;
                    return std::isnan(a) || std::isnan(b) ? 2 : a < b ? -1 : a > b ? 1 : 0;
                }
                // (an int and a float: exactly)
                bool const floatLeft = (lk == NEOS_PY_FLOAT);
                double const f = floatLeft ? as<float_object>(aLhs)->value : as<float_object>(aRhs)->value;
                if (std::isnan(f))
                    return 2;
                if (is_big(floatLeft ? aRhs : aLhs))
                {
                    // (the float's integer part exactly; a float beyond 2^53 has no fractional part)
                    auto const& i = big_of(floatLeft ? aRhs : aLhs);
                    int order = 0;
                    if (std::isinf(f))
                        order = f > 0.0 ? 1 : -1;
                    else
                    {
                        auto const t = big_from_double(f);
                        order = t < i ? -1 : i < t ? 1 : (f > std::trunc(f) ? 1 : f < std::trunc(f) ? -1 : 0);
                    }
                    return floatLeft ? order : -order;
                }
                long long const i = floatLeft ? int_value(aRhs) : int_value(aLhs);
                int order;
                if (f >= 9223372036854775808.0)
                    order = 1;
                else if (f < -9223372036854775808.0)
                    order = -1;
                else
                {
                    auto const t = std::trunc(f);
                    auto const ti = static_cast<long long>(t);
                    order = ti < i ? -1 : ti > i ? 1 : (f > t ? 1 : f < t ? -1 : 0);
                }
                return floatLeft ? order : -order;
            }
            if ((lk == NEOS_PY_COMPLEX && (rnum || rk == NEOS_PY_COMPLEX)) || (rk == NEOS_PY_COMPLEX && lnum))
            {
                double ar = 0.0, ai = 0.0, br = 0.0, bi = 0.0;
                if (lk == NEOS_PY_COMPLEX) { ar = as<complex_object>(aLhs)->real; ai = as<complex_object>(aLhs)->imag; } else to_double(aLhs, ar);
                if (rk == NEOS_PY_COMPLEX) { br = as<complex_object>(aRhs)->real; bi = as<complex_object>(aRhs)->imag; } else to_double(aRhs, br);
                return ar == br && ai == bi ? 0 : 4; // (4: not equal, unorderable)
            }
            if (lk == NEOS_PY_STR && rk == NEOS_PY_STR)
            {
                auto const c = as<str_object>(aLhs)->value.compare(as<str_object>(aRhs)->value);
                return c < 0 ? -1 : c > 0 ? 1 : 0;
            }
            if (lk == NEOS_PY_LIST && rk == NEOS_PY_LIST)
                return sequence_order(as<list_object>(aLhs)->items, as<list_object>(aRhs)->items, aError, aEquality);
            if (lk == NEOS_PY_TUPLE && rk == NEOS_PY_TUPLE)
                return sequence_order(as<tuple_object>(aLhs)->items, as<tuple_object>(aRhs)->items, aError, aEquality);
            if (lk == NEOS_PY_NONE && rk == NEOS_PY_NONE)
                return 0;
            if (lk == NEOS_PY_DICT && rk == NEOS_PY_DICT)
            {
                auto const& a = as<dict_object>(aLhs)->items;
                auto const& b = as<dict_object>(aRhs)->items;
                if (a.size() != b.size())
                    return 4;
                for (std::size_t i = 0u; i < a.size(); ++i)
                {
                    // (dict_equal: each key's value in the other dict)
                    ref const key = ref::borrowed(a[i].first);
                    ref const value = ref::borrowed(a[i].second);
                    auto const position = dict_find(*as<dict_object>(aRhs), key.get(), as<dict_object>(aLhs)->hashes[i]);
                    if (position == -2) { aError = 1; return 0; }
                    if (position < 0)
                        return 4;
                    auto const e = equal(value.get(), b[static_cast<std::size_t>(position)].second);
                    if (e < 0) { aError = 1; return 0; }
                    if (e == 0)
                        return 4;
                }
                return 0;
            }
            if (lk == NEOS_PY_RANGE && rk == NEOS_PY_RANGE)
            {
                auto const& a = *as<range_object>(aLhs);
                auto const& b = *as<range_object>(aRhs);
                return a.start == b.start && a.stop == b.stop && a.step == b.step ? 0 : 4;
            }
            return aLhs == aRhs ? 0 : 3;
        }

        object* compare(int aOperation, object* aLhs, object* aRhs)
        {
            // (two ints of 64 bits: their values')
            if (is_small_int(aLhs) && is_small_int(aRhs))
            {
                auto const a = as<int_object>(aLhs)->value;
                auto const b = as<int_object>(aRhs)->value;
                switch (aOperation)
                {
                case NEOS_PY_EQ: return make_bool(a == b);
                case NEOS_PY_NE: return make_bool(a != b);
                case NEOS_PY_LT: return make_bool(a < b);
                case NEOS_PY_LE: return make_bool(a <= b);
                case NEOS_PY_GT: return make_bool(a > b);
                case NEOS_PY_GE: return make_bool(a >= b);
                default: break;
                }
            }
            // (two strs: their texts' (UTF-8's byte order is its code points'))
            if (kind_of(aLhs) == NEOS_PY_STR && kind_of(aRhs) == NEOS_PY_STR)
            {
                int const order = aLhs == aRhs ? 0 : as<str_object>(aLhs)->value.compare(as<str_object>(aRhs)->value);
                switch (aOperation)
                {
                case NEOS_PY_EQ: return make_bool(order == 0);
                case NEOS_PY_NE: return make_bool(order != 0);
                case NEOS_PY_LT: return make_bool(order < 0);
                case NEOS_PY_LE: return make_bool(order <= 0);
                case NEOS_PY_GT: return make_bool(order > 0);
                case NEOS_PY_GE: return make_bool(order >= 0);
                default: break;
                }
            }
            auto const lt = python_type(aLhs);
            auto const rt = python_type(aRhs);
            if (kind_of(aLhs) == NEOS_PY_OTHER && lt != nullptr && lt->compare != nullptr)
            {
                ref result{ lt->compare(context(), aOperation, aLhs, aRhs) };
                if (!result || kind_of(result.get()) != NEOS_PY_NOT_IMPLEMENTED)
                    return result.release_ownership();
            }
            if (kind_of(aRhs) == NEOS_PY_OTHER && rt != nullptr && rt->compare != nullptr && rt != lt)
            {
                ref result{ rt->compare(context(), aOperation, aLhs, aRhs) };
                if (!result || kind_of(result.get()) != NEOS_PY_NOT_IMPLEMENTED)
                    return result.release_ownership();
            }
            int failure = 0;
            auto const order = core_order(aLhs, aRhs, failure, aOperation == NEOS_PY_EQ || aOperation == NEOS_PY_NE);
            if (failure != 0)
                return nullptr;
            switch (aOperation)
            {
            case NEOS_PY_EQ: return make_bool(order == 0);
            case NEOS_PY_NE: return make_bool(order != 0);
            default:
                if (order == 3 || order == 4)
                    return error("TypeError", std::string{ "'" } + comparison_symbol(aOperation) + "' not supported between instances of '" + type_name(aLhs) + "' and '" + type_name(aRhs) + "'");
                if (order == 2)
                    return make_bool(false);
                switch (aOperation)
                {
                case NEOS_PY_LT: return make_bool(order < 0);
                case NEOS_PY_LE: return make_bool(order <= 0);
                case NEOS_PY_GT: return make_bool(order > 0);
                default: return make_bool(order >= 0);
                }
            }
        }

        long long length(object* aObject)
        {
            switch (kind_of(aObject))
            {
            case NEOS_PY_STR: return as<str_object>(aObject)->len();
            case NEOS_PY_LIST: return static_cast<long long>(as<list_object>(aObject)->items.size());
            case NEOS_PY_TUPLE: return static_cast<long long>(as<tuple_object>(aObject)->items.size());
            case NEOS_PY_DICT: return static_cast<long long>(as<dict_object>(aObject)->items.size());
            case NEOS_PY_RANGE:
                {
                    auto const& r = *as<range_object>(aObject);
                    if (r.step > 0)
                        return r.stop > r.start ? (r.stop - r.start + r.step - 1) / r.step : 0;
                    return r.stop < r.start ? (r.start - r.stop - r.step - 1) / -r.step : 0;
                }
            case NEOS_PY_OTHER:
                if (auto const type = python_type(aObject); type != nullptr && type->length != nullptr)
                    return type->length(context(), aObject);
                break;
            default:
                break;
            }
            error("TypeError", std::string{ "object of type '" } + type_name(aObject) + "' has no len()");
            return -1;
        }

        int truth(object* aObject)
        {
            switch (kind_of(aObject))
            {
            case NEOS_PY_NONE: return 0;
            case NEOS_PY_BOOL: return aObject == &sTrue.base ? 1 : 0;
            case NEOS_PY_INT: return is_big(aObject) || as<int_object>(aObject)->value != 0 ? 1 : 0;
            case NEOS_PY_FLOAT: return as<float_object>(aObject)->value != 0.0 ? 1 : 0;
            case NEOS_PY_COMPLEX: return as<complex_object>(aObject)->real != 0.0 || as<complex_object>(aObject)->imag != 0.0 ? 1 : 0;
            case NEOS_PY_STR: return as<str_object>(aObject)->value.empty() ? 0 : 1;
            case NEOS_PY_LIST: case NEOS_PY_TUPLE: case NEOS_PY_DICT: case NEOS_PY_RANGE:
                return length(aObject) != 0 ? 1 : 0;
            case NEOS_PY_OTHER:
                if (auto const type = python_type(aObject))
                {
                    if (type->truth != nullptr)
                        return type->truth(context(), aObject);
                    if (type->length != nullptr)
                    {
                        auto const n = type->length(context(), aObject);
                        return n < 0 ? -1 : n != 0 ? 1 : 0;
                    }
                }
                return 1;
            default:
                return 1;
            }
        }

        // a sequence index (negative: from the end) in range
        bool normalize_index(object* aKey, std::size_t aSize, std::size_t& aIndex, char const* aWhat)
        {
            long long i = 0;
            if (!to_index(aKey, i))
            {
                // (an index too large for an index-sized integer: IndexError (PyNumber_AsSsize_t(key, PyExc_IndexError)); a range's: out of range)
                ref const e{ take_exception() };
                if (!exception_is(e.get(), "OverflowError"))
                {
                    reraise_exception(e.get());
                    return false;
                }
                error("IndexError", std::strcmp(aWhat, "range object") == 0 ? "range object index out of range" : "cannot fit 'int' into an index-sized integer");
                return false;
            }
            if (i < 0)
                i += static_cast<long long>(aSize);
            if (i < 0 || i >= static_cast<long long>(aSize))
            {
                error("IndexError", std::string{ aWhat } + " index out of range");
                return false;
            }
            aIndex = static_cast<std::size_t>(i);
            return true;
        }

        // Python's slice.indices()
        bool slice_indices(object* aSlice, long long aLength, long long& aStart, long long& aStop, long long& aStep)
        {
            auto const& s = *as<slice_object>(aSlice);
            aStep = 1;
            if (kind_of(s.step) != NEOS_PY_NONE && !to_index(s.step, aStep))
                return false;
            if (aStep == 0)
            {
                error("ValueError", "slice step cannot be zero");
                return false;
            }
            auto const bound = [&](object* aPart, long long aDefault, long long& aResult)
            {
                if (kind_of(aPart) == NEOS_PY_NONE)
                {
                    aResult = aDefault;
                    return true;
                }
                if (!to_index(aPart, aResult))
                    return false;
                if (aResult < 0)
                {
                    aResult += aLength;
                    if (aResult < 0)
                        aResult = aStep < 0 ? -1 : 0;
                }
                else if (aResult >= aLength)
                    aResult = aStep < 0 ? aLength - 1 : aLength;
                return true;
            };
            return bound(s.start, aStep < 0 ? aLength - 1 : 0, aStart) && bound(s.stop, aStep < 0 ? -1 : aLength, aStop);
        }

        std::vector<std::size_t> slice_positions(long long aStart, long long aStop, long long aStep)
        {
            std::vector<std::size_t> positions;
            if (aStep > 0)
                for (long long i = aStart; i < aStop; i += aStep)
                    positions.push_back(static_cast<std::size_t>(i));
            else
                for (long long i = aStart; i > aStop; i += aStep)
                    positions.push_back(static_cast<std::size_t>(i));
            return positions;
        }

        std::vector<std::string_view> code_points(std::string const& aText)
        {
            std::vector<std::string_view> result;
            for (std::size_t i = 0u; i < aText.size();)
            {
                auto const b = static_cast<unsigned char>(aText[i]);
                std::size_t const n = b < 0x80u ? 1u : b < 0xE0u ? 2u : b < 0xF0u ? 3u : 4u;
                result.emplace_back(aText.data() + i, std::min(n, aText.size() - i));
                i += n;
            }
            return result;
        }

        // (the str of an ASCII character: one of the 128 that are made once (they are never released), as CPython's single characters are)
        object* ascii_character(char aCharacter)
        {
            static object* sCharacters[128] = {};
            auto& character = sCharacters[static_cast<unsigned char>(aCharacter) & 0x7Fu];
            if (character == nullptr)
            {
                character = make_str(std::string(1u, aCharacter), 1u);
            }
            retain(character);
            return character;
        }

        object* getitem(object* aObject, object* aKey)
        {
            auto const k = kind_of(aObject);
            if (k == NEOS_PY_LIST || k == NEOS_PY_TUPLE)
            {
                auto const& items = k == NEOS_PY_LIST ? as<list_object>(aObject)->items : as<tuple_object>(aObject)->items;
                if (kind_of(aKey) == NEOS_PY_SLICE)
                {
                    long long start = 0, stop = 0, step = 0;
                    if (!slice_indices(aKey, static_cast<long long>(items.size()), start, stop, step))
                        return nullptr;
                    std::vector<object*> result;
                    for (auto i : slice_positions(start, stop, step))
                    {
                        retain(items[i]);
                        result.push_back(items[i]);
                    }
                    return k == NEOS_PY_LIST ? make_list(std::move(result)) : make_tuple(std::move(result));
                }
                std::size_t i = 0u;
                if (!normalize_index(aKey, items.size(), i, k == NEOS_PY_LIST ? "list" : "tuple"))
                    return nullptr;
                retain(items[i]);
                return items[i];
            }
            if (k == NEOS_PY_STR)
            {
                auto const& s = *as<str_object>(aObject);
                if (kind_of(aKey) == NEOS_PY_SLICE)
                {
                    long long start = 0, stop = 0, step = 0;
                    if (!slice_indices(aKey, s.len(), start, stop, step))
                        return nullptr;
                    std::string result;
                    if (s.is_ascii())
                    {
                        // (ASCII: its characters are its bytes)
                        if (step == 1)
                            result.assign(s.value, static_cast<std::size_t>(start), static_cast<std::size_t>(std::max(stop - start, 0LL)));
                        else
                            for (auto i : slice_positions(start, stop, step))
                                result += s.ascii_at(i);
                        auto const n = result.size();
                        return make_str(std::move(result), n);    // (known: ASCII)
                    }
                    // (its code points: see neos::runtime::string)
                    std::size_t n = 0u;
                    for (auto i : slice_positions(start, stop, step))
                    {
                        neos::runtime::string_detail::encode(s.at(static_cast<long long>(i)), result);
                        ++n;
                    }
                    return make_str(std::move(result), n);
                }
                std::size_t i = 0u;
                if (!normalize_index(aKey, static_cast<std::size_t>(s.len()), i, "string"))
                    return nullptr;
                auto const character = s.at(static_cast<long long>(i));
                if (character < 0x80u)
                    return ascii_character(static_cast<char>(character));
                return make_str(neos::runtime::utf8_of(character), 1u);
            }
            if (k == NEOS_PY_RANGE)
            {
                auto const& r = *as<range_object>(aObject);
                std::size_t i = 0u;
                if (!normalize_index(aKey, static_cast<std::size_t>(length(aObject)), i, "range object"))
                    return nullptr;
                return make_int(r.start + static_cast<long long>(i) * r.step);
            }
            if (k == NEOS_PY_DICT)
            {
                auto const position = dict_find(aObject, aKey);
                if (position == -2)
                    return nullptr;
                if (position >= 0)
                {
                    auto const value = as<dict_object>(aObject)->items[static_cast<std::size_t>(position)].second;
                    retain(value);
                    return value;
                }
                raise_key_error(aKey);
                return nullptr;
            }
            if (k == NEOS_PY_OTHER)
                if (auto const type = python_type(aObject); type != nullptr && type->getitem != nullptr)
                    return type->getitem(context(), aObject, aKey);
            return error("TypeError", std::string{ "'" } + type_name(aObject) + "' object is not subscriptable");
        }

        // --- dict keys: found as CPython finds them (the same object, or an equal hash and ==); a dict of more than a few items 
        // has an index of its items' positions by their keys' hashes

        constexpr std::size_t kDictIndexMinimum = 8u;

        void dict_index_insert(dict_object& aDict, std::size_t aPosition)
        {
            auto const mask = aDict.index.size() - 1u;
            auto i = static_cast<std::size_t>(aDict.hashes[aPosition]) & mask;
            while (aDict.index[i] != -1)
                i = (i + 1u) & mask;
            aDict.index[i] = static_cast<std::int32_t>(aPosition);
        }

        void dict_reindex(dict_object& aDict)
        {
            std::size_t size = 16u;
            while (size < aDict.items.size() * 2u)
                size <<= 1;
            aDict.index.assign(size, -1);
            for (std::size_t i = 0u; i < aDict.items.size(); ++i)
                dict_index_insert(aDict, i);
        }

        long long dict_find(dict_object& aDict, object* aKey, long long aHash)
        {
            auto const matches = [&](std::size_t aPosition) -> int
            {
                auto const key = aDict.items[aPosition].first;
                if (key == aKey)
                    return 1;
                if (aDict.hashes[aPosition] != aHash)
                    return 0;
                ref const k = ref::borrowed(key);
                return equal(key, aKey);
            };
            if (aDict.items.size() < kDictIndexMinimum)
            {
                for (std::size_t i = 0u; i < aDict.items.size(); ++i)
                    if (auto const m = matches(i); m != 0)
                        return m < 0 ? -2 : static_cast<long long>(i);
                return -1;
            }
            if (aDict.index.empty())
                dict_reindex(aDict);
            auto const mask = aDict.index.size() - 1u;
            for (auto i = static_cast<std::size_t>(aHash) & mask; aDict.index[i] != -1; i = (i + 1u) & mask)
                if (auto const m = matches(static_cast<std::size_t>(aDict.index[i])); m != 0)
                    return m < 0 ? -2 : aDict.index[i];
            return -1;
        }

        long long dict_find(object* aDict, object* aKey)
        {
            long long hash = 0;
            if (!hash_of(aKey, hash))
                return -2;
            return dict_find(*as<dict_object>(aDict), aKey, hash);
        }

        int dict_set(object* aDict, object* aKey, object* aValue)
        {
            auto& d = *as<dict_object>(aDict);
            if (d.binder != nullptr && !namespace_bind(d.binder, aKey, aValue))
                return -1;
            long long hash = 0;
            if (!hash_of(aKey, hash))
                return -1;
            auto const position = dict_find(d, aKey, hash);
            if (position == -2)
                return -1;
            if (position >= 0)
            {
                auto& item = d.items[static_cast<std::size_t>(position)];
                retain(aValue);
                release(item.second);
                item.second = aValue;
                return 0;
            }
            retain(aKey);
            retain(aValue);
            d.items.emplace_back(aKey, aValue);
            d.hashes.push_back(hash);
            if (d.items.size() >= kDictIndexMinimum)
            {
                if (d.index.empty() || d.items.size() * 2u > d.index.size())
                    dict_reindex(d);
                else
                    dict_index_insert(d, d.items.size() - 1u);
            }
            return 0;
        }

        void dict_erase(dict_object& aDict, std::size_t aPosition)
        {
            release(aDict.items[aPosition].first);
            release(aDict.items[aPosition].second);
            aDict.items.erase(aDict.items.begin() + static_cast<std::ptrdiff_t>(aPosition));
            aDict.hashes.erase(aDict.hashes.begin() + static_cast<std::ptrdiff_t>(aPosition));
            aDict.index.clear();
        }

        object* iter(object* aObject);
        object* next(object* aIterator);

        bool collect(object* aIterable, std::vector<object*>& aItems)
        {
            ref const it{ iter(aIterable) };
            if (!it)
                return false;
            for (;;)
            {
                auto const item = next(it.get());
                if (item == nullptr)
                    return !failed();
                aItems.push_back(item);
            }
        }

        int setitem(object* aObject, object* aKey, object* aValue)
        {
            auto const k = kind_of(aObject);
            if (k == NEOS_PY_LIST)
            {
                auto& items = as<list_object>(aObject)->items;
                if (kind_of(aKey) == NEOS_PY_SLICE)
                {
                    long long start = 0, stop = 0, step = 0;
                    if (!slice_indices(aKey, static_cast<long long>(items.size()), start, stop, step))
                        return -1;
                    std::vector<object*> values;
                    if (aValue != nullptr && !collect(aValue, values))
                        return -1;
                    if (step == 1)
                    {
                        stop = std::max(stop, start);
                        for (auto i = start; i < stop; ++i)
                            release(items[static_cast<std::size_t>(i)]);
                        items.erase(items.begin() + start, items.begin() + stop);
                        items.insert(items.begin() + start, values.begin(), values.end());
                        return 0;
                    }
                    auto const positions = slice_positions(start, stop, step);
                    if (aValue == nullptr)
                    {
                        auto sorted = positions;
                        std::sort(sorted.rbegin(), sorted.rend());
                        for (auto i : sorted)
                        {
                            release(items[i]);
                            items.erase(items.begin() + static_cast<std::ptrdiff_t>(i));
                        }
                        return 0;
                    }
                    if (positions.size() != values.size())
                    {
                        for (auto v : values) release(v);
                        error("ValueError", "attempt to assign sequence of size " + std::to_string(values.size()) + " to extended slice of size " + std::to_string(positions.size()));
                        return -1;
                    }
                    for (std::size_t i = 0u; i < positions.size(); ++i)
                    {
                        release(items[positions[i]]);
                        items[positions[i]] = values[i];
                    }
                    return 0;
                }
                std::size_t i = 0u;
                if (!normalize_index(aKey, items.size(), i, "list assignment"))
                    return -1;
                if (aValue == nullptr)
                {
                    release(items[i]);
                    items.erase(items.begin() + static_cast<std::ptrdiff_t>(i));
                    return 0;
                }
                retain(aValue);
                release(items[i]);
                items[i] = aValue;
                return 0;
            }
            if (k == NEOS_PY_DICT)
            {
                if (aValue != nullptr)
                    return dict_set(aObject, aKey, aValue);
                auto const position = dict_find(aObject, aKey);
                if (position == -2)
                    return -1;
                if (position >= 0)
                {
                    dict_erase(*as<dict_object>(aObject), static_cast<std::size_t>(position));
                    return 0;
                }
                raise_key_error(aKey);
                return -1;
            }
            if (k == NEOS_PY_OTHER)
                if (auto const type = python_type(aObject); type != nullptr && type->setitem != nullptr)
                    return type->setitem(context(), aObject, aKey, aValue);
            error("TypeError", std::string{ "'" } + type_name(aObject) + (aValue != nullptr ? "' object does not support item assignment" : "' object does not support item deletion"));
            return -1;
        }

        object* iter(object* aObject)
        {
            switch (kind_of(aObject))
            {
            case NEOS_PY_LIST: case NEOS_PY_TUPLE: case NEOS_PY_STR: case NEOS_PY_RANGE:
                return make_iterator(aObject);
            case NEOS_PY_DICT:
                return dict_iterator(aObject, 0);
            case NEOS_PY_ITERATOR:
                retain(aObject);
                return aObject;
            case NEOS_PY_OTHER:
                if (auto const type = python_type(aObject))
                {
                    if (type->iter != nullptr)
                        return type->iter(context(), aObject);
                    if (type->getitem != nullptr && type->length != nullptr)
                        return make_iterator(aObject);
                    if (type->next != nullptr)
                    {
                        retain(aObject);
                        return aObject;
                    }
                }
                break;
            default:
                break;
            }
            return error("TypeError", std::string{ "'" } + type_name(aObject) + "' object is not iterable");
        }

        object* next(object* aIterator)
        {
            if (kind_of(aIterator) == NEOS_PY_ITERATOR)
            {
                auto& it = *as<iterator_object>(aIterator);
                auto const sequence = it.sequence;
                switch (kind_of(sequence))
                {
                case NEOS_PY_LIST:
                case NEOS_PY_TUPLE:
                    {
                        auto const& items = kind_of(sequence) == NEOS_PY_LIST ? as<list_object>(sequence)->items : as<tuple_object>(sequence)->items;
                        if (it.index >= items.size())
                            return nullptr;
                        retain(items[it.index]);
                        return items[it.index++];
                    }
                case NEOS_PY_STR:
                    {
                        auto const& text = as<str_object>(sequence)->value;
                        if (it.index >= text.size())
                            return nullptr;
                        auto const b = static_cast<unsigned char>(text[it.index]);
                        std::size_t const n = b < 0x80u ? 1u : b < 0xE0u ? 2u : b < 0xF0u ? 3u : 4u;
                        auto const result = n == 1u ? ascii_character(static_cast<char>(b)) : make_str(text.substr(it.index, n));
                        it.index += n;
                        return result;
                    }
                case NEOS_PY_DICT:
                    {
                        auto const& items = as<dict_object>(sequence)->items;
                        if (it.index >= items.size())
                            return nullptr;
                        retain(items[it.index].first);
                        return items[it.index++].first;
                    }
                case NEOS_PY_RANGE:
                    {
                        if (it.count < 0)
                            it.count = length(sequence);
                        if (static_cast<long long>(it.index) >= it.count)
                            return nullptr;
                        auto const& r = *as<range_object>(sequence);
                        return make_int(r.start + static_cast<long long>(it.index++) * r.step);
                    }
                default:
                    {
                        // (a sequence with __len__ and __getitem__)
                        auto const n = length(sequence);
                        if (n < 0)
                            return nullptr;
                        if (static_cast<long long>(it.index) >= n)
                            return nullptr;
                        ref const index{ make_int(static_cast<long long>(it.index++)) };
                        return getitem(sequence, index.get());
                    }
                }
            }
            if (auto const type = python_type(aIterator); type != nullptr && type->next != nullptr)
                return type->next(context(), aIterator);
            return error("TypeError", std::string{ "'" } + type_name(aIterator) + "' object is not an iterator");
        }

        int contains(object* aContainer, object* aItem)
        {
            switch (kind_of(aContainer))
            {
            case NEOS_PY_STR:
                if (kind_of(aItem) != NEOS_PY_STR)
                {
                    error("TypeError", std::string{ "'in <string>' requires string as left operand, not " } + type_name(aItem));
                    return -1;
                }
                return as<str_object>(aContainer)->value.find(as<str_object>(aItem)->value) != std::string::npos ? 1 : 0;
            case NEOS_PY_DICT:
                {
                    auto const position = dict_find(aContainer, aItem);
                    return position == -2 ? -1 : position >= 0 ? 1 : 0;
                }
            case NEOS_PY_OTHER:
                if (auto const type = python_type(aContainer); type != nullptr && type->contains != nullptr)
                    return type->contains(context(), aContainer, aItem);
                [[fallthrough]];
            default:
                {
                    auto const k = kind_of(aContainer);
                    auto const type = python_type(aContainer);
                    bool const iterable = k == NEOS_PY_LIST || k == NEOS_PY_TUPLE || k == NEOS_PY_RANGE || k == NEOS_PY_ITERATOR ||
                        (k == NEOS_PY_OTHER && type != nullptr && (type->iter != nullptr || type->next != nullptr || (type->getitem != nullptr && type->length != nullptr)));
                    if (!iterable)
                    {
                        error("TypeError", std::string{ "argument of type '" } + type_name(aContainer) + "' is not iterable");
                        return -1;
                    }
                    ref const it{ iter(aContainer) };
                    if (!it)
                        return -1;
                    for (;;)
                    {
                        ref const item{ next(it.get()) };
                        if (!item)
                            return failed() ? -1 : 0;
                        if (item.get() == aItem)
                            return 1;
                        ref const equal{ compare(NEOS_PY_EQ, item.get(), aItem) };
                        if (!equal)
                            return -1;
                        auto const t = truth(equal.get());
                        if (t != 0)
                            return t;
                    }
                }
            }
        }

        // --- methods of the core types

        struct method_data
        {
            object* (*function)(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs);
        };

        object* call_method(neos_py_context*, void* aData, neos_py_object* aArgs, neos_py_object* aKwargs);

        std::vector<object*> const& tuple_items(object* aTuple) { return as<tuple_object>(aTuple)->items; }

#if defined(_MSC_VER)
        __declspec(noinline)
#else
        __attribute__((noinline))
#endif
        bool arity_error(std::vector<object*> const& aArgs, std::size_t aMin, std::size_t aMax, char const* aName);
        inline bool arity(std::vector<object*> const& aArgs, std::size_t aMin, std::size_t aMax, char const* aName)
        {
            return (aArgs.size() >= aMin && aArgs.size() <= aMax) || arity_error(aArgs, aMin, aMax, aName);
        }
        // (arity's: out of line, so that its check is cheap)
        bool arity_error(std::vector<object*> const& aArgs, std::size_t aMin, std::size_t aMax, char const* aName)
        {
            if (aMin == aMax)
                error("TypeError", std::string{ aName } + "() takes exactly " + std::to_string(aMin) + " argument" + (aMin == 1u ? "" : "s") + " (" + std::to_string(aArgs.size()) + " given)");
            else
                error("TypeError", std::string{ aName } + "() expected at most " + std::to_string(aMax) + " argument" + (aMax == 1u ? "" : "s") + ", got " + std::to_string(aArgs.size()));
            return false;
        }

        object* list_append(object* aSelf, std::vector<object*> const& aArgs, object*)
        {
            if (!arity(aArgs, 1u, 1u, "list.append"))
                return nullptr;
            retain(aArgs[0]);
            as<list_object>(aSelf)->items.push_back(aArgs[0]);
            return none();
        }

        object* list_extend(object* aSelf, std::vector<object*> const& aArgs, object*)
        {
            if (!arity(aArgs, 1u, 1u, "list.extend"))
                return nullptr;
            std::vector<object*> items;
            if (!collect(aArgs[0], items))
                return nullptr;
            auto& list = as<list_object>(aSelf)->items;
            list.insert(list.end(), items.begin(), items.end());
            return none();
        }

        object* list_pop(object* aSelf, std::vector<object*> const& aArgs, object*)
        {
            if (!arity(aArgs, 0u, 1u, "pop"))
                return nullptr;
            auto& items = as<list_object>(aSelf)->items;
            if (items.empty())
                return error("IndexError", "pop from empty list");
            std::size_t i = items.size() - 1u;
            if (!aArgs.empty())
            {
                long long index = 0;
                if (!to_index(aArgs[0], index))
                    return nullptr;
                if (index < 0)
                    index += static_cast<long long>(items.size());
                if (index < 0 || index >= static_cast<long long>(items.size()))
                    return error("IndexError", "pop index out of range");
                i = static_cast<std::size_t>(index);
            }
            auto const result = items[i];
            items.erase(items.begin() + static_cast<std::ptrdiff_t>(i));
            return result;
        }

        object* list_insert(object* aSelf, std::vector<object*> const& aArgs, object*)
        {
            if (!arity(aArgs, 2u, 2u, "insert"))
                return nullptr;
            long long index = 0;
            if (!to_index(aArgs[0], index))
                return nullptr;
            auto& items = as<list_object>(aSelf)->items;
            auto const n = static_cast<long long>(items.size());
            if (index < 0)
                index = std::max(0ll, index + n);
            index = std::min(index, n);
            retain(aArgs[1]);
            items.insert(items.begin() + index, aArgs[1]);
            return none();
        }

        object* sequence_count(object* aSelf, std::vector<object*> const& aArgs, object*)
        {
            if (!arity(aArgs, 1u, 1u, "count"))
                return nullptr;
            auto const& items = kind_of(aSelf) == NEOS_PY_LIST ? as<list_object>(aSelf)->items : as<tuple_object>(aSelf)->items;
            long long n = 0;
            for (auto item : items)
            {
                ref const equal{ compare(NEOS_PY_EQ, item, aArgs[0]) };
                if (!equal)
                    return nullptr;
                n += truth(equal.get()) == 1 ? 1 : 0;
            }
            return make_int(n);
        }

        object* list_reverse(object* aSelf, std::vector<object*> const& aArgs, object*)
        {
            if (!arity(aArgs, 0u, 0u, "reverse"))
                return nullptr;
            auto& items = as<list_object>(aSelf)->items;
            std::reverse(items.begin(), items.end());
            return none();
        }

        object* list_copy(object* aSelf, std::vector<object*> const& aArgs, object*)
        {
            if (!arity(aArgs, 0u, 0u, "copy"))
                return nullptr;
            auto items = as<list_object>(aSelf)->items;
            for (auto i : items)
                retain(i);
            return make_list(std::move(items));
        }

        object* dict_get_method(object* aSelf, std::vector<object*> const& aArgs, object*)
        {
            if (!arity(aArgs, 1u, 2u, "get"))
                return nullptr;
            auto const position = dict_find(aSelf, aArgs[0]);
            if (position == -2)
                return nullptr;
            if (position >= 0)
            {
                auto const value = as<dict_object>(aSelf)->items[static_cast<std::size_t>(position)].second;
                retain(value);
                return value;
            }
            auto const result = aArgs.size() > 1u ? aArgs[1] : none();
            retain(result);
            return result;
        }

        bool format_string(std::string const& aFormat, std::vector<object*> const& aArgs, object* aKwargs, std::string& aResult, int aDepth);

        object* str_format(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs)
        {
            std::string result;
            if (!format_string(as<str_object>(aSelf)->value, aArgs, aKwargs, result, 0))
                return nullptr;
            return make_str(std::move(result));
        }

        // int's and float's methods
        object* int_bit_length(object* aSelf, std::vector<object*> const& aArgs, object*)
        {
            if (!arity(aArgs, 0u, 0u, "int.bit_length"))
                return nullptr;
            if (limb_view v; limbs_of(aSelf, v))
                return make_int(static_cast<long long>(limbs_bits(v.data, v.size)));
            return make_int(static_cast<long long>(big_bit_length(big_value(aSelf))));
        }
        object* int_bit_count(object* aSelf, std::vector<object*> const& aArgs, object*)
        {
            if (!arity(aArgs, 0u, 0u, "int.bit_count"))
                return nullptr;
            if (limb_view v; limbs_of(aSelf, v))
            {
                long long count = 0;
                for (std::uint32_t i = 0u; i < v.size; ++i)
                    count += std::popcount(v.data[i]);
                return make_int(count);
            }
            auto const magnitude = big_abs(big_value(aSelf));
            long long count = 0;
            for (std::uint32_t i = 0u; i < big_detail::magnitude(magnitude); ++i)
                count += std::popcount(static_cast<std::uint64_t>(big_detail::cword(magnitude, i)));
            return make_int(count);
        }
        object* int_conjugate(object* aSelf, std::vector<object*> const& aArgs, object*)
        {
            if (!arity(aArgs, 0u, 0u, "int.conjugate"))
                return nullptr;
            return is_big(aSelf) ? (retain(aSelf), aSelf) : make_int(int_value(aSelf));
        }
        object* int_as_integer_ratio(object* aSelf, std::vector<object*> const& aArgs, object*)
        {
            if (!arity(aArgs, 0u, 0u, "int.as_integer_ratio"))
                return nullptr;
            return make_tuple({ is_big(aSelf) ? (retain(aSelf), aSelf) : make_int(int_value(aSelf)), make_int(1) });
        }
        object* int_is_integer(object*, std::vector<object*> const& aArgs, object*)
        {
            if (!arity(aArgs, 0u, 0u, "int.is_integer"))
                return nullptr;
            return make_bool(true);
        }
        object* float_is_integer(object* aSelf, std::vector<object*> const& aArgs, object*)
        {
            if (!arity(aArgs, 0u, 0u, "float.is_integer"))
                return nullptr;
            auto const x = as<float_object>(aSelf)->value;
            return make_bool(std::isfinite(x) && std::floor(x) == x);
        }
        object* float_as_integer_ratio(object* aSelf, std::vector<object*> const& aArgs, object*)
        {
            if (!arity(aArgs, 0u, 0u, "float.as_integer_ratio"))
                return nullptr;
            auto const x = as<float_object>(aSelf)->value;
            if (std::isinf(x))
                return error("OverflowError", "cannot convert Infinity to integer ratio");
            if (std::isnan(x))
                return error("ValueError", "cannot convert NaN to integer ratio");
            int e = 0;
            auto m = std::frexp(x, &e);
            // (the mantissa as an integer: x = m * 2^e exactly)
            for (int i = 0; i < 300 && m != std::floor(m); ++i)
            {
                m *= 2.0;
                --e;
            }
            big_integer numerator{ static_cast<long long>(m) };
            big_integer denominator{ 1 };
            if (e > 0)
                numerator = numerator << static_cast<std::int64_t>(e);
            else
                denominator = denominator << static_cast<std::int64_t>(-e);
            return make_tuple({ make_int(std::move(numerator)), make_int(std::move(denominator)) });
        }

        // int.to_bytes(length=1, byteorder='big', *, signed=False)
        object* int_to_bytes(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs)
        {
            std::vector<object*> a;
            if (!parse_arguments("to_bytes", aArgs, aKwargs, { "length", "byteorder", "signed" }, 0u, 0u, a))
                return nullptr;
            if (aArgs.size() > 2u)
                return error("TypeError", "to_bytes() takes at most 2 positional arguments (" + std::to_string(aArgs.size()) + " given)");
            long long length = 1;
            if (a[0] != nullptr && !to_index(a[0], length))
                return nullptr;
            if (length < 0)
                return error("ValueError", "length argument must be non-negative");
            std::string order = "big";
            if (a[1] != nullptr)
            {
                if (kind_of(a[1]) != NEOS_PY_STR)
                    return error("TypeError", std::string{ "to_bytes() argument 'byteorder' must be str, not " } + type_name(a[1]));
                order = as<str_object>(a[1])->value;
                if (order != "big" && order != "little")
                    return error("ValueError", "byteorder must be either 'little' or 'big'");
            }
            bool const isSigned = a[2] != nullptr && truth(a[2]) == 1;
            // (an int's limbs: as two's complement bytes)
            if (limb_view v; limbs_of(aSelf, v))
            {
                bool const negative = v.negative && v.size != 0u;
                if (negative && !isSigned)
                    return error("OverflowError", "can't convert negative int to unsigned");
                auto const bits = limbs_bits(v.data, v.size);
                auto const limit = static_cast<std::uint64_t>(8 * length);
                // (signed: one bit fewer, other than for -2^(8 length - 1))
                bool fits = bits <= limit;
                if (isSigned && bits == limit && bits != 0u)
                {
                    std::uint64_t ones = 0u;
                    for (std::uint32_t i = 0u; i < v.size; ++i)
                        ones += static_cast<std::uint64_t>(std::popcount(v.data[i]));
                    fits = negative && ones == 1u;
                }
                if (!fits)
                    return error("OverflowError", "int too big to convert");
                std::string bytes(static_cast<std::size_t>(length), '\0');
                for (std::size_t w = 0u, i = 0u; w < v.size && i < bytes.size(); ++w)
                    for (unsigned shift = 0u; shift < 64u && i < bytes.size(); shift += 8u)
                        bytes[i++] = static_cast<char>((v.data[w] >> shift) & 0xFFu);
                if (negative)
                {
                    unsigned carry = 1u;
                    for (auto& byte : bytes)
                    {
                        auto const sum = (~static_cast<unsigned>(static_cast<unsigned char>(byte)) & 0xFFu) + carry;
                        byte = static_cast<char>(sum & 0xFFu);
                        carry = sum >> 8u;
                    }
                }
                if (order == "big")
                    std::reverse(bytes.begin(), bytes.end());
                return make_bytes(std::move(bytes));
            }
            auto value = big_value(aSelf);
            if (big_negative(value))
            {
                if (!isSigned)
                    return error("OverflowError", "can't convert negative int to unsigned");
                // (two's complement (at least -2^(8 length - 1)))
                value = (big_integer{ 1 } << static_cast<std::int64_t>(8 * length)) + value;
                if (big_negative(value) || length == 0 || big_bit_length(value) < static_cast<std::uint64_t>(8 * length))
                    return error("OverflowError", "int too big to convert");
            }
            else if (isSigned && length > 0 && big_bit_length(value) >= static_cast<std::uint64_t>(8 * length))
                return error("OverflowError", "int too big to convert");
            if (big_bit_length(value) > static_cast<std::uint64_t>(8 * length))
                return error("OverflowError", "int too big to convert");
            auto hex = big_digits(value, 16);
            if (hex == "0")
                hex.clear();
            if (hex.size() % 2u != 0u)
                hex.insert(hex.begin(), '0');
            std::string bytes(static_cast<std::size_t>(length), '\0');
            auto const n = hex.size() / 2u;
            for (std::size_t i = 0u; i < n; ++i)
            {
                auto const digit = [](char c) { return c >= 'a' ? c - 'a' + 10 : c >= 'A' ? c - 'A' + 10 : c - '0'; };
                bytes[static_cast<std::size_t>(length) - n + i] = static_cast<char>((digit(hex[2u * i]) << 4) | digit(hex[2u * i + 1u]));
            }
            if (order == "little")
                std::reverse(bytes.begin(), bytes.end());
            return make_bytes(std::move(bytes));
        }

        // int.from_bytes(bytes, byteorder='big', *, signed=False) (a classmethod: aSelf is the class)
        object* int_from_bytes(neos_py_context*, void*, object* aArgs, object* aKwargs)
        {
            auto const& args = tuple_items(aArgs);
            std::vector<object*> a;
            if (!parse_arguments("from_bytes", args, aKwargs, { "bytes", "byteorder", "signed" }, 1u, 0u, a))
                return nullptr;
            std::string data;
            std::string_view view;
            if (bytes_like(a[0], view))
                data = std::string{ view };
            else
            {
                ref const converted{ b_bytes({ a[0] }, nullptr) };
                if (!converted || !bytes_like(converted.get(), view))
                    return nullptr;
                data = std::string{ view };
            }
            std::string order = "big";
            if (a[1] != nullptr)
            {
                if (kind_of(a[1]) != NEOS_PY_STR)
                    return error("TypeError", std::string{ "from_bytes() argument 'byteorder' must be str, not " } + type_name(a[1]));
                order = as<str_object>(a[1])->value;
                if (order != "big" && order != "little")
                    return error("ValueError", "byteorder must be either 'little' or 'big'");
            }
            if (order == "little")
                std::reverse(data.begin(), data.end());
            big_integer value{ 0 };
            for (unsigned char c : data)
                value = (value << 8) + big_integer{ static_cast<long long>(c) };
            if (a[2] != nullptr && truth(a[2]) == 1 && !data.empty() && (static_cast<unsigned char>(data[0]) & 0x80u) != 0u)
                value = value - (big_integer{ 1 } << static_cast<std::int64_t>(8 * data.size()));
            return make_int(std::move(value));
        }

        struct method_entry
        {
            int kind;
            char const* name;
            method_data data;
        };

        method_entry const sMethods[] =
        {
            { NEOS_PY_LIST, "append", { list_append } },
            { NEOS_PY_LIST, "extend", { list_extend } },
            { NEOS_PY_LIST, "pop", { list_pop } },
            { NEOS_PY_LIST, "insert", { list_insert } },
            { NEOS_PY_LIST, "count", { sequence_count } },
            { NEOS_PY_LIST, "reverse", { list_reverse } },
            { NEOS_PY_LIST, "copy", { list_copy } },
            { NEOS_PY_TUPLE, "count", { sequence_count } },
            { NEOS_PY_DICT, "get", { dict_get_method } },
            { NEOS_PY_STR, "format", { str_format } },
            { NEOS_PY_INT, "bit_length", { int_bit_length } },
            { NEOS_PY_INT, "bit_count", { int_bit_count } },
            { NEOS_PY_INT, "conjugate", { int_conjugate } },
            { NEOS_PY_INT, "as_integer_ratio", { int_as_integer_ratio } },
            { NEOS_PY_INT, "is_integer", { int_is_integer } },
            { NEOS_PY_INT, "to_bytes", { int_to_bytes } },
            { NEOS_PY_BOOL, "to_bytes", { int_to_bytes } },
            { NEOS_PY_BOOL, "bit_length", { int_bit_length } },
            { NEOS_PY_BOOL, "bit_count", { int_bit_count } },
            { NEOS_PY_BOOL, "conjugate", { int_conjugate } },
            { NEOS_PY_BOOL, "as_integer_ratio", { int_as_integer_ratio } },
            { NEOS_PY_BOOL, "is_integer", { int_is_integer } },
            { NEOS_PY_FLOAT, "is_integer", { float_is_integer } },
            { NEOS_PY_FLOAT, "as_integer_ratio", { float_as_integer_ratio } }
        };

        object* call_method(neos_py_context*, void* aData, neos_py_object* aArgs, neos_py_object* aKwargs)
        {
            // (a bound method: the function object's self is the first argument)
            auto const& args = tuple_items(aArgs);
            auto const* data = static_cast<method_entry const*>(aData);
            if (args.empty())
                return error("TypeError", "unbound method");
            if (auto const k = kind_of(args[0]); k != data->kind && !(data->kind == NEOS_PY_INT && k == NEOS_PY_BOOL))
            {
                auto const type = data->kind == NEOS_PY_LIST ? "list" : data->kind == NEOS_PY_TUPLE ? "tuple" : data->kind == NEOS_PY_DICT ? "dict" : 
                    data->kind == NEOS_PY_STR ? "str" : data->kind == NEOS_PY_FLOAT ? "float" : "int";
                return error("TypeError", std::string{ "descriptor '" } + data->name + "' for '" + type + "' objects doesn't apply to a '" + type_name(args[0]) + "' object");
            }
            std::vector<object*> rest{ args.begin() + 1, args.end() };
            if (aKwargs != nullptr && as<dict_object>(aKwargs)->items.size() != 0u && data->data.function != str_format && data->data.function != int_to_bytes)
                return error("TypeError", std::string{ data->name } + "() takes no keyword arguments");
            return data->data.function(args[0], rest, aKwargs);
        }

        object* class_of(object* aObject);

        // (an object of a builtin type: the method of a slot that its type has (e.g. an iterator's __next__))
        object* slot_method(object* aObject, char const* aName)
        {
            auto const type = python_type(aObject);
            bool const iterator = kind_of(aObject) == NEOS_PY_ITERATOR || (type != nullptr && type->next != nullptr);
            if (std::strcmp(aName, "__next__") == 0 && iterator)
                return make_function("__next__", [](neos_py_context*, void*, object* aArgs, object*) -> object*
                    {
                        auto const& items = as<tuple_object>(aArgs)->items;
                        auto const result = next(items.at(0u));
                        if (result == nullptr && !failed())
                            return error("StopIteration", "");
                        return result;
                    }, nullptr, aObject);
            if (std::strcmp(aName, "__iter__") == 0 && iterator)
                return make_function("__iter__", [](neos_py_context*, void*, object* aArgs, object*) -> object*
                    {
                        auto const self = as<tuple_object>(aArgs)->items.at(0u);
                        retain(self);
                        return self;
                    }, nullptr, aObject);
            if (type == nullptr)
                return nullptr;
            std::string_view const name{ aName };
            if ((name == "__len__" && type->length != nullptr) || (name == "__getitem__" && type->getitem != nullptr) ||
                (name == "__contains__" && type->contains != nullptr) || (name == "__iter__" && type->iter != nullptr) ||
                (name == "__repr__" && type->repr != nullptr) || (name == "__hash__" && type->hash != nullptr) ||
                ((name == "__setitem__" || name == "__delitem__") && type->setitem != nullptr) ||
                ((name == "__eq__" || name == "__ne__" || name == "__lt__" || name == "__le__" || name == "__gt__" || name == "__ge__") && type->compare != nullptr) ||
                ((name == "__add__" || name == "__mul__" || name == "__rmul__" || name == "__iadd__" || name == "__imul__") && type->binary != nullptr))
                return protocol_method(aObject, -1, aName);
            if (name == "__call__" && type->call != nullptr && kind_of(aObject) == NEOS_PY_OTHER)
                return make_function("__call__", [](neos_py_context*, void*, object* aArgs, object* aKwargs) -> object*
                    {
                        auto const& items = as<tuple_object>(aArgs)->items;
                        ref const rest{ make_tuple(std::vector<object*>{ items.begin() + 1, items.end() }) };
                        for (auto a : tuple_items(rest.get()))
                            retain(a);
                        return call(items.at(0u), rest.get(), aKwargs);
                    }, nullptr, aObject);
            return nullptr;
        }

        object* object_attribute(object* aSelf, std::string_view aName);
        object* number_rounding_method(char const* aName, object* aSelf);
        object* class_function(std::string const& aClass, char const* aName);

        bool is_instance(object* aObject);
        object* instance_attribute_cached(object* aObject, char const* aName);
        bool instance_set_cached(object* aObject, char const* aName, object* aValue);

        object* getattr(object* aObject, char const* aName)
        {
            // (an instance's own attribute, found before)
            if (is_instance(aObject))
                if (auto const result = instance_attribute_cached(aObject, aName))
                    return result;
            auto const k = kind_of(aObject);
            if (k == NEOS_PY_OTHER)
            {
                if (auto const type = python_type(aObject); type != nullptr && type->getattr != nullptr)
                {
                    auto const result = type->getattr(context(), aObject, aName);
                    if (result == nullptr && !failed())
                        error("AttributeError", std::string{ "'" } + type_name(aObject) + "' object has no attribute '" + aName + "'");
                    if (result == nullptr && std::strcmp(aName, "__class__") == 0 && std::string_view{ error_text() }.starts_with("AttributeError"))
                    {
                        clear_error();
                        return class_of(aObject);
                    }
                    if (result == nullptr && aName[0] == '_' && aName[1] == '_' && std::string_view{ error_text() }.starts_with("AttributeError"))
                        if (auto const method = slot_method(aObject, aName))
                        {
                            clear_error();
                            return method;
                        }
                    return result;
                }
                if (aName[0] == '_' && aName[1] == '_')
                    if (auto const method = slot_method(aObject, aName))
                        return method;
            }
            else
            {
                if (k == NEOS_PY_ITERATOR && aName[0] == '_' && aName[1] == '_')
                    if (auto const method = slot_method(aObject, aName))
                        return method;
                for (auto const& method : sMethods)
                    if (method.kind == k && std::strcmp(method.name, aName) == 0)
                        return make_function(method.name, call_method, const_cast<method_entry*>(&method), aObject);
                if (k == NEOS_PY_STR)
                    if (auto const method = str_method(aObject, aName))
                        return method;
                if (k == NEOS_PY_LIST || k == NEOS_PY_TUPLE || k == NEOS_PY_DICT)
                    if (auto const method = container_method(aObject, k, aName))
                        return method;
                if (auto const method = protocol_method(aObject, k, aName))
                    return method;
                if (k == NEOS_PY_INT || k == NEOS_PY_BOOL || k == NEOS_PY_FLOAT)
                    if (auto const method = number_rounding_method(aName, aObject))
                        return method;
                if (k == NEOS_PY_COMPLEX)
                {
                    if (std::strcmp(aName, "real") == 0) return make_float(as<complex_object>(aObject)->real);
                    if (std::strcmp(aName, "imag") == 0) return make_float(as<complex_object>(aObject)->imag);
                    if (std::strcmp(aName, "conjugate") == 0)
                        return make_function("conjugate", [](neos_py_context*, void*, object* aArgs, object*) -> object*
                            {
                                auto const& items = as<tuple_object>(aArgs)->items;
                                if (items.size() != 1u)
                                    return error("TypeError", "complex.conjugate() takes no arguments (" + std::to_string(items.size() - 1u) + " given)");
                                auto const& c = *as<complex_object>(items[0]);
                                return make_complex(c.real, -c.imag);
                            }, nullptr, aObject);
                }
                if (k == NEOS_PY_SLICE)
                {
                    auto const& s = *as<slice_object>(aObject);
                    object* part = std::strcmp(aName, "start") == 0 ? s.start : std::strcmp(aName, "stop") == 0 ? s.stop : std::strcmp(aName, "step") == 0 ? s.step : nullptr;
                    if (part != nullptr) { retain(part); return part; }
                }
                if ((k == NEOS_PY_INT || k == NEOS_PY_BOOL || k == NEOS_PY_FLOAT) && (std::strcmp(aName, "real") == 0 || std::strcmp(aName, "imag") == 0))
                {
                    if (std::strcmp(aName, "real") == 0) 
                    { 
                        if (k == NEOS_PY_BOOL)
                            return make_int(int_value(aObject));
                        retain(aObject); 
                        return aObject; 
                    }
                    return k == NEOS_PY_FLOAT ? make_float(0.0) : make_int(0);
                }
                if ((k == NEOS_PY_INT || k == NEOS_PY_BOOL) && (std::strcmp(aName, "numerator") == 0 || std::strcmp(aName, "denominator") == 0))
                {
                    if (std::strcmp(aName, "denominator") == 0)
                        return make_int(1);
                    return k == NEOS_PY_BOOL ? make_int(int_value(aObject)) : (retain(aObject), aObject);
                }
            }
            // (every object's class)
            if (std::strcmp(aName, "__class__") == 0)
            {
                clear_error();
                return class_of(aObject);
            }
            if (k == NEOS_PY_FUNCTION)
            {
                // (a builtin function's attributes)
                auto const& f = *as<function_object>(aObject);
                if (std::strcmp(aName, "__name__") == 0)
                    return make_str(f.name);
                if (std::strcmp(aName, "__qualname__") == 0)
                {
                    if (f.self != nullptr && !is_class(f.self) && !is_type_object(f.self) && kind_of(f.self) != NEOS_PY_OTHER)
                        return make_str(std::string{ type_name(f.self) } + "." + f.name);
                    return make_str(f.name);
                }
                if (std::strcmp(aName, "__self__") == 0)
                    return new_reference(f.self != nullptr ? f.self : none());
                if (std::strcmp(aName, "__isabstractmethod__") == 0 && abstract_builtins().contains(aObject))
                    return make_bool(true);
                if (std::strcmp(aName, "__module__") == 0)
                    return f.self == nullptr ? make_str("builtins") : new_reference(none());
                if (std::strcmp(aName, "__doc__") == 0 || std::strcmp(aName, "__text_signature__") == 0)
                    return new_reference(none());
                if (std::strcmp(aName, "__eq__") == 0 || std::strcmp(aName, "__ne__") == 0 || std::strcmp(aName, "__hash__") == 0 || 
                    std::strcmp(aName, "__repr__") == 0 || std::strcmp(aName, "__str__") == 0)
                    if (auto const a = object_attribute(aObject, aName))
                        return a;
            }
            // (object(): all of object's methods)
            if (k == NEOS_PY_OTHER && std::strcmp(aObject->type->name, "object") == 0)
            {
                if (auto const a = object_attribute(aObject, aName))
                {
                    clear_error();
                    return a;
                }
                if (ref const unbound{ class_function("object", aName) }; unbound && kind_of(unbound.get()) == NEOS_PY_FUNCTION)
                {
                    clear_error();
                    if (std::strcmp(aName, "__new__") == 0 || std::strcmp(aName, "__init_subclass__") == 0)
                        return new_reference(unbound.get());
                    return make_function(aName, as<function_object>(unbound.get())->function, as<function_object>(unbound.get())->data, aObject);
                }
                clear_error();
            }
            // (object's methods that every object has)
            {
                static std::set<std::string, std::less<>> const sCommon = { "__format__", "__sizeof__", "__reduce_ex__", "__reduce__", "__dir__", 
                    "__getstate__", "__subclasshook__" };
                if (sCommon.contains(aName))
                    if (auto const a = object_attribute(aObject, aName))
                    {
                        clear_error();
                        return a;
                    }
            }
            return error("AttributeError", std::string{ "'" } + type_name(aObject) + "' object has no attribute '" + aName + "'");
        }

        int setattr(object* aObject, char const* aName, object* aValue)
        {
            // (an instance's own attribute, set before)
            if (aValue != nullptr && is_instance(aObject) && instance_set_cached(aObject, aName, aValue))
                return 0;
            if (auto const type = python_type(aObject); type != nullptr && type->setattr != nullptr && kind_of(aObject) == NEOS_PY_OTHER)
                return type->setattr(context(), aObject, aName, aValue);
            error("AttributeError", std::string{ "'" } + type_name(aObject) + "' object has no attribute '" + aName + "'");
            return -1;
        }

        bool is_pending_method(object* aObject);
        object* arguments_call(object* aCallable, std::span<object* const> aItems, object* aArgs, object* aKwargs);

        object* call(object* aCallable, object* aArgs, object* aKwargs)
        {
            // (load_method's method)
            if (is_pending_method(aCallable))
                return arguments_call(aCallable, aArgs != nullptr ? std::span<object* const>{ tuple_items(aArgs) } : std::span<object* const>{}, aArgs, aKwargs);
            ref args;
            if (aArgs == nullptr)
                args = ref{ make_tuple() };
            else
                args = ref::borrowed(aArgs);
            if (kind_of(aCallable) == NEOS_PY_FUNCTION)
            {
                auto const& f = *as<function_object>(aCallable);
                if (f.self != nullptr)
                {
                    // (a bound method)
                    std::vector<object*> items{ f.self };
                    retain(f.self);
                    for (auto item : tuple_items(args.get()))
                    {
                        retain(item);
                        items.push_back(item);
                    }
                    ref const bound{ make_tuple(std::move(items)) };
                    return f.function(context(), f.data, bound.get(), aKwargs);
                }
                return f.function(context(), f.data, args.get(), aKwargs);
            }
            if (auto const type = python_type(aCallable); type != nullptr && type->call != nullptr && kind_of(aCallable) == NEOS_PY_OTHER)
                return type->call(context(), aCallable, args.get(), aKwargs);
            return error("TypeError", std::string{ "'" } + type_name(aCallable) + "' object is not callable");
        }

        // --- formatting: format(value, spec) (the format specification mini-language), str.format and str % values

        std::size_t code_point_count(std::string const& aText)
        {
            std::size_t n = 0u;
            for (auto c : aText)
                if ((static_cast<unsigned char>(c) & 0xC0u) != 0x80u)
                    ++n;
            return n;
        }

        struct format_spec
        {
            std::string fill;
            char align = 0;
            char sign = 0;
            bool z = false;
            bool alternate = false;
            bool zero = false;
            long long width = -1;
            char grouping = 0;
            long long precision = -1;
            char type = 0;
        };

        bool parse_format_spec(std::string const& aSpec, format_spec& aResult, char const* aTypeName)
        {
            auto const is_align = [](char c) { return c == '<' || c == '>' || c == '=' || c == '^'; };
            std::size_t i = 0u;
            // ([fill]align: the fill is one code point)
            std::size_t fillLength = 0u;
            if (!aSpec.empty())
            {
                auto const lead = static_cast<unsigned char>(aSpec[0]);
                fillLength = lead < 0x80u ? 1u : lead >= 0xF0u ? 4u : lead >= 0xE0u ? 3u : 2u;
            }
            if (fillLength != 0u && aSpec.size() > fillLength && is_align(aSpec[fillLength]))
            {
                aResult.fill = aSpec.substr(0u, fillLength);
                aResult.align = aSpec[fillLength];
                i = fillLength + 1u;
            }
            else if (!aSpec.empty() && is_align(aSpec[0]))
            {
                aResult.align = aSpec[0];
                i = 1u;
            }
            if (i < aSpec.size() && (aSpec[i] == '+' || aSpec[i] == '-' || aSpec[i] == ' '))
                aResult.sign = aSpec[i++];
            if (i < aSpec.size() && aSpec[i] == 'z')
            {
                aResult.z = true;
                ++i;
            }
            if (i < aSpec.size() && aSpec[i] == '#')
            {
                aResult.alternate = true;
                ++i;
            }
            if (i < aSpec.size() && aSpec[i] == '0')
            {
                aResult.zero = true;
                ++i;
            }
            auto const digits = [&](long long& aValue)
            {
                bool any = false;
                aValue = 0;
                while (i < aSpec.size() && aSpec[i] >= '0' && aSpec[i] <= '9')
                {
                    aValue = aValue * 10 + (aSpec[i++] - '0');
                    any = true;
                }
                return any;
            };
            long long width = 0;
            if (digits(width))
                aResult.width = width;
            if (i < aSpec.size() && (aSpec[i] == ',' || aSpec[i] == '_'))
                aResult.grouping = aSpec[i++];
            if (i < aSpec.size() && (aSpec[i] == ',' || aSpec[i] == '_'))
            {
                error("ValueError", "Cannot specify both ',' and '_'.");
                return false;
            }
            if (i < aSpec.size() && aSpec[i] == '.')
            {
                ++i;
                long long precision = 0;
                if (!digits(precision))
                {
                    error("ValueError", "Format specifier missing precision");
                    return false;
                }
                aResult.precision = precision;
            }
            if (i < aSpec.size())
                aResult.type = aSpec[i++];
            if (i < aSpec.size())
            {
                error("ValueError", "Invalid format specifier '" + aSpec + "' for object of type '" + aTypeName + "'");
                return false;
            }
            return true;
        }

        // (digits grouped with aSeparator every aEvery digits)
        std::string group_digits(std::string const& aDigits, char aSeparator, std::size_t aEvery)
        {
            if (aSeparator == 0)
                return aDigits;
            std::string result;
            auto const n = aDigits.size();
            for (std::size_t i = 0u; i < n; ++i)
            {
                if (i != 0u && (n - i) % aEvery == 0u)
                    result += aSeparator;
                result += aDigits[i];
            }
            return result;
        }

        // (the sign, then the body, aligned and padded)
        std::string pad_formatted(std::string const& aSign, std::string const& aBody, format_spec const& aSpec, char aDefaultAlign)
        {
            std::string fill = aSpec.fill.empty() ? " " : aSpec.fill;
            char align = aSpec.align;
            if (aSpec.zero)
            {
                if (aSpec.fill.empty())
                    fill = "0";
                if (align == 0)
                    align = '=';
            }
            if (align == 0)
                align = aDefaultAlign;
            auto const length = static_cast<long long>(code_point_count(aSign) + code_point_count(aBody));
            if (aSpec.width <= length)
                return aSign + aBody;
            auto const n = static_cast<std::size_t>(aSpec.width - length);
            auto const repeat = [&](std::size_t aCount) { std::string s; for (std::size_t k = 0u; k < aCount; ++k) s += fill; return s; };
            switch (align)
            {
            case '<': return aSign + aBody + repeat(n);
            case '^': return repeat(n / 2u) + aSign + aBody + repeat(n - n / 2u);
            case '=': return aSign + repeat(n) + aBody;
            default: return repeat(n) + aSign + aBody;
            }
        }

        std::string sign_of(bool aNegative, char aSign)
        {
            return aNegative ? "-" : aSign == '+' ? "+" : aSign == ' ' ? " " : "";
        }

        // (the digits of a double: to_chars in a format with a precision)
        std::string chars_of(double aValue, std::chars_format aFormat, int aPrecision)
        {
            char buffer[512];
            auto const converted = std::to_chars(buffer, buffer + sizeof(buffer), aValue, aFormat, aPrecision);
            if (converted.ec != std::errc{})
            {
                // (very large fixed: printf)
                std::vector<char> big(1024u + static_cast<std::size_t>(aPrecision));
                std::snprintf(big.data(), big.size(), aFormat == std::chars_format::fixed ? "%.*f" : "%.*e", aPrecision, aValue);
                return big.data();
            }
            return std::string{ buffer, static_cast<std::size_t>(converted.ptr - buffer) };
        }

        // (C's %g: aValue >= 0)
        std::string general_format(double aValue, long long aPrecision, bool aAlternate, bool aUpper)
        {
            auto const p = static_cast<int>(aPrecision == 0 ? 1 : aPrecision);
            auto const scientific = chars_of(aValue, std::chars_format::scientific, p - 1);
            auto const exponent = std::stoi(scientific.substr(scientific.find('e') + 1u));
            std::string result;
            if (exponent >= -4 && exponent < p)
                result = chars_of(aValue, std::chars_format::fixed, p - 1 - exponent);
            else
                result = scientific;
            if (!aAlternate)
            {
                auto const e = result.find('e');
                std::string mantissa = result.substr(0u, e);
                std::string const rest = e == std::string::npos ? std::string{} : result.substr(e);
                if (mantissa.find('.') != std::string::npos)
                {
                    while (!mantissa.empty() && mantissa.back() == '0')
                        mantissa.pop_back();
                    if (!mantissa.empty() && mantissa.back() == '.')
                        mantissa.pop_back();
                }
                result = mantissa + rest;
            }
            else if (result.find('.') == std::string::npos)
            {
                auto const e = result.find('e');
                result.insert(e == std::string::npos ? result.size() : e, ".");
            }
            if (aUpper)
                for (auto& c : result)
                    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return result;
        }

        bool format_float(double aValue, format_spec const& aSpec, std::string& aResult)
        {
            auto type = aSpec.type;
            if (type != 0 && std::string_view{ "eEfFgGn%" }.find(type) == std::string_view::npos)
            {
                error("ValueError", std::string{ "Unknown format code '" } + type + "' for object of type 'float'");
                return false;
            }
            if (aSpec.alternate && false)
                return false;
            bool const upper = type == 'E' || type == 'F' || type == 'G';
            bool negative = std::signbit(aValue);
            double x = std::fabs(aValue);
            std::string body;
            if (std::isnan(aValue) || std::isinf(aValue))
            {
                body = std::isnan(aValue) ? (upper ? "NAN" : "nan") : (upper ? "INF" : "inf");
                if (type == '%')
                    body += "%";
            }
            else
            {
                auto const precision = aSpec.precision < 0 ? 6 : aSpec.precision;
                switch (type)
                {
                case 'e':
                case 'E':
                    body = chars_of(x, std::chars_format::scientific, static_cast<int>(precision));
                    if (aSpec.alternate && precision == 0)
                        body.insert(body.find('e'), ".");
                    if (upper)
                        body[body.find('e')] = 'E';
                    break;
                case 'f':
                case 'F':
                case '%':
                    body = chars_of(type == '%' ? x * 100.0 : x, std::chars_format::fixed, static_cast<int>(precision));
                    if (aSpec.alternate && precision == 0)
                        body += ".";
                    if (type == '%')
                        body += "%";
                    break;
                case 'g':
                case 'G':
                case 'n':
                    body = general_format(x, precision, aSpec.alternate, upper);
                    break;
                default:
                    if (aSpec.precision < 0)
                        body = python_float(x);
                    else
                    {
                        body = general_format(x, aSpec.precision, aSpec.alternate, false);
                        if (body.find_first_of(".e") == std::string::npos)
                            body += ".0";
                    }
                    break;
                }
                if (aSpec.z && negative && body.find_first_not_of("0.%") == std::string::npos)
                    negative = false;
                if (aSpec.grouping != 0)
                {
                    auto const end = body.find_first_not_of("0123456789");
                    body = group_digits(body.substr(0u, end), aSpec.grouping, 3u) + (end == std::string::npos ? std::string{} : body.substr(end));
                }
            }
            aResult = pad_formatted(sign_of(negative, aSpec.sign), body, aSpec, '>');
            return true;
        }

        // (aBig: the value if it is beyond 64 bits)
        bool format_int(long long aValue, format_spec const& aSpec, std::string& aResult, big_integer const* aBig = nullptr)
        {
            auto const type = aSpec.type;
            if (type == 'e' || type == 'E' || type == 'f' || type == 'F' || type == 'g' || type == 'G' || type == '%')
            {
                double x = static_cast<double>(aValue);
                if (aBig != nullptr && !big_to_double(*aBig, x))
                {
                    error("OverflowError", "int too large to convert to float");
                    return false;
                }
                return format_float(x, aSpec, aResult);
            }
            if (type != 0 && std::string_view{ "bcdoxXn" }.find(type) == std::string_view::npos)
            {
                error("ValueError", std::string{ "Unknown format code '" } + type + "' for object of type 'int'");
                return false;
            }
            if (aSpec.precision >= 0)
            {
                error("ValueError", "Precision not allowed in integer format specifier");
                return false;
            }
            if (aSpec.grouping == ',' && type != 0 && type != 'd')
            {
                error("ValueError", std::string{ "Cannot specify ',' with '" } + type + "'.");
                return false;
            }
            if (type == 'c')
            {
                if (aSpec.sign != 0)
                {
                    error("ValueError", "Sign not allowed with integer format specifier 'c'");
                    return false;
                }
                if (aBig != nullptr || aValue < 0 || aValue > 0x10FFFF)
                {
                    error("OverflowError", "%c arg not in range(0x110000)");
                    return false;
                }
                std::string body;
                auto const c = static_cast<std::uint32_t>(aValue);
                if (c < 0x80u) body += static_cast<char>(c);
                else if (c < 0x800u) { body += static_cast<char>(0xC0u | (c >> 6)); body += static_cast<char>(0x80u | (c & 0x3Fu)); }
                else if (c < 0x10000u) { body += static_cast<char>(0xE0u | (c >> 12)); body += static_cast<char>(0x80u | ((c >> 6) & 0x3Fu)); body += static_cast<char>(0x80u | (c & 0x3Fu)); }
                else { body += static_cast<char>(0xF0u | (c >> 18)); body += static_cast<char>(0x80u | ((c >> 12) & 0x3Fu)); body += static_cast<char>(0x80u | ((c >> 6) & 0x3Fu)); body += static_cast<char>(0x80u | (c & 0x3Fu)); }
                aResult = pad_formatted("", body, aSpec, '<');
                return true;
            }
            bool const negative = aBig != nullptr ? big_negative(*aBig) : aValue < 0;
            auto const magnitude = negative ? 0ull - static_cast<unsigned long long>(aValue) : static_cast<unsigned long long>(aValue);
            int base = 10;
            std::string prefix;
            switch (type)
            {
            case 'b': base = 2; prefix = "0b"; break;
            case 'o': base = 8; prefix = "0o"; break;
            case 'x': base = 16; prefix = "0x"; break;
            case 'X': base = 16; prefix = "0X"; break;
            default: break;
            }
            char buffer[80];
            auto const converted = std::to_chars(buffer, buffer + sizeof(buffer), magnitude, base);
            std::string digits{ buffer, static_cast<std::size_t>(converted.ptr - buffer) };
            if (aBig != nullptr)
            {
                digits = big_digits(*aBig, base);
                if (base == 10 && sIntMaxStrDigits > 0 && digits.size() > static_cast<std::size_t>(sIntMaxStrDigits))
                {
                    int_max_str_digits_error(digits.size(), false);
                    return false;
                }
            }
            if (type == 'X')
                for (auto& c : digits)
                    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            digits = group_digits(digits, aSpec.grouping, base == 10 ? 3u : 4u);
            auto const sign = sign_of(negative, aSpec.sign) + (aSpec.alternate ? prefix : std::string{});
            aResult = pad_formatted(sign, digits, aSpec, '>');
            return true;
        }

        bool format_str(std::string const& aValue, format_spec const& aSpec, std::string& aResult)
        {
            if (aSpec.type != 0 && aSpec.type != 's')
            {
                error("ValueError", std::string{ "Unknown format code '" } + aSpec.type + "' for object of type 'str'");
                return false;
            }
            if (aSpec.sign != 0)
            {
                error("ValueError", "Sign not allowed in string format specifier");
                return false;
            }
            if (aSpec.alternate)
            {
                error("ValueError", "Alternate form (#) not allowed in string format specifier");
                return false;
            }
            if (aSpec.align == '=')
            {
                error("ValueError", "'=' alignment not allowed in string format specifier");
                return false;
            }
            std::string body = aValue;
            if (aSpec.precision >= 0)
            {
                auto const points = code_points(aValue);
                body.clear();
                for (std::size_t i = 0u; i < points.size() && static_cast<long long>(i) < aSpec.precision; ++i)
                    body += points[i];
            }
            auto spec = aSpec;
            if (spec.zero && spec.align == 0)
                spec.align = '<';
            aResult = pad_formatted("", body, spec, '<');
            return true;
        }

        // format(value, spec)
        bool format_value(object* aValue, std::string const& aSpec, std::string& aResult)
        {
            auto const k = kind_of(aValue);
            if (aSpec.empty())
            {
                // (an instance of a class that has a __format__: its format of "")
                if (is_instance(aValue))
                    if (ref const method{ special_method(aValue, "__format__") })
                    {
                        ref const args{ make_tuple({ make_str("") }) };
                        ref const text{ call(method.get(), args.get(), nullptr) };
                        if (!text)
                            return false;
                        if (kind_of(text.get()) != NEOS_PY_STR)
                        {
                            error("TypeError", std::string{ "__format__ must return a str, not " } + type_name(text.get()));
                            return false;
                        }
                        aResult = as<str_object>(text.get())->value;
                        return true;
                    }
                return str_of(aValue, aResult);
            }
            format_spec spec;
            switch (k)
            {
            case NEOS_PY_BOOL:
            case NEOS_PY_INT:
                return parse_format_spec(aSpec, spec, "int") && format_int(is_big(aValue) ? 0 : int_value(aValue), spec, aResult, is_big(aValue) ? &big_of(aValue) : nullptr);
            case NEOS_PY_FLOAT:
                return parse_format_spec(aSpec, spec, "float") && format_float(as<float_object>(aValue)->value, spec, aResult);
            case NEOS_PY_STR:
                return parse_format_spec(aSpec, spec, "str") && format_str(as<str_object>(aValue)->value, spec, aResult);
            case NEOS_PY_OTHER:
                {
                    // (another plugin's object: an index is an int, else a float (as NumPy's scalars))
                    auto const type = python_type(aValue);
                    // (its __format__, if any)
                    if (type != nullptr && type->getattr != nullptr)
                    {
                        if (auto const method = type->getattr(context(), aValue, "__format__"))
                        {
                            ref const formatter{ method };
                            ref specText{ make_str(aSpec) };
                            ref const args{ make_tuple({ specText.release_ownership() }) };
                            ref const text{ call(formatter.get(), args.get(), nullptr) };
                            if (!text)
                                return false;
                            if (kind_of(text.get()) != NEOS_PY_STR)
                            {
                                error("TypeError", std::string{ "__format__ must return a str, not " } + type_name(text.get()));
                                return false;
                            }
                            aResult = as<str_object>(text.get())->value;
                            return true;
                        }
                        clear_error();
                    }
                    if (!parse_format_spec(aSpec, spec, type_name(aValue)))
                        return false;
                    bool const integral = spec.type != 0 && std::string_view{ "bcdoxX" }.find(spec.type) != std::string_view::npos;
                    if (type != nullptr && type->index != nullptr && (integral || spec.type == 0))
                    {
                        long long value = 0;
                        if (type->index(context(), aValue, &value) == 0)
                            return format_int(value, spec, aResult);
                        clear_error();
                    }
                    if (type != nullptr && type->to_float != nullptr && !integral)
                    {
                        double value = 0.0;
                        if (type->to_float(context(), aValue, &value) == 0)
                            return format_float(value, spec, aResult);
                        clear_error();
                    }
                    std::string text;
                    if (!str_of(aValue, text))
                        return false;
                    if (spec.type == 0 || spec.type == 's')
                        return format_str(text, spec, aResult);
                    error("TypeError", std::string{ "unsupported format string passed to " } + type_name(aValue) + ".__format__");
                    return false;
                }
            default:
                error("TypeError", std::string{ "unsupported format string passed to " } + type_name(aValue) + ".__format__");
                return false;
            }
        }

        object* getitem(object* aObject, object* aKey);
        object* getattr(object* aObject, char const* aName);

        // (a value converted by !r, !s or !a)
        bool convert_field(object* aValue, char aConversion, std::string const& aSpec, std::string& aResult)
        {
            if (aConversion == 0)
                return format_value(aValue, aSpec, aResult);
            std::string text;
            if (aConversion == 'r' || aConversion == 'a')
            {
                if (!repr_of(aValue, text))
                    return false;
                if (aConversion == 'a')
                {
                    std::string ascii;
                    for (auto const& point : code_points(text))
                    {
                        if (point.size() == 1u)
                        {
                            ascii += point;
                            continue;
                        }
                        std::uint32_t c = static_cast<unsigned char>(point[0]) & (point.size() == 2u ? 0x1Fu : point.size() == 3u ? 0x0Fu : 0x07u);
                        for (std::size_t i = 1u; i < point.size(); ++i)
                            c = (c << 6) | (static_cast<unsigned char>(point[i]) & 0x3Fu);
                        char buffer[16];
                        std::snprintf(buffer, sizeof(buffer), c <= 0xFFu ? "\\x%02x" : c <= 0xFFFFu ? "\\u%04x" : "\\U%08x", c);
                        ascii += buffer;
                    }
                    text = ascii;
                }
            }
            else if (aConversion == 's')
            {
                if (!str_of(aValue, text))
                    return false;
            }
            else
            {
                error("ValueError", std::string{ "Unknown conversion specifier " } + aConversion);
                return false;
            }
            ref const converted{ make_str(text) };
            return format_value(converted.get(), aSpec, aResult);
        }

        // str.format
        bool format_string(std::string const& aFormat, std::vector<object*> const& aArgs, object* aKwargs, std::string& aResult, int aDepth = 0)
        {
            if (aDepth > 2)
            {
                error("ValueError", "Max string recursion exceeded");
                return false;
            }
            std::size_t automatic = 0u;
            bool usedAutomatic = false, usedManual = false;
            std::size_t i = 0u;
            auto const n = aFormat.size();
            while (i < n)
            {
                auto const c = aFormat[i];
                if (c == '}')
                {
                    if (i + 1u < n && aFormat[i + 1u] == '}')
                    {
                        aResult += '}';
                        i += 2u;
                        continue;
                    }
                    error("ValueError", "Single '}' encountered in format string");
                    return false;
                }
                if (c != '{')
                {
                    aResult += c;
                    ++i;
                    continue;
                }
                if (i + 1u < n && aFormat[i + 1u] == '{')
                {
                    aResult += '{';
                    i += 2u;
                    continue;
                }
                // (a replacement field: to its matching '}')
                std::size_t depth = 1u, j = i + 1u;
                for (; j < n && depth != 0u; ++j)
                {
                    if (aFormat[j] == '{') ++depth;
                    else if (aFormat[j] == '}') --depth;
                }
                if (depth != 0u)
                {
                    error("ValueError", "expected '}' before end of string");
                    return false;
                }
                auto const field = aFormat.substr(i + 1u, j - i - 2u);
                i = j;
                // field_name [!conversion] [:spec]
                std::size_t nameEnd = 0u;
                for (std::size_t bracket = 0u; nameEnd < field.size(); ++nameEnd)
                {
                    if (field[nameEnd] == '[') ++bracket;
                    else if (field[nameEnd] == ']') --bracket;
                    else if (bracket == 0u && (field[nameEnd] == '!' || field[nameEnd] == ':'))
                        break;
                }
                auto const name = field.substr(0u, nameEnd);
                char conversion = 0;
                std::string spec;
                std::size_t k = nameEnd;
                if (k < field.size() && field[k] == '!')
                {
                    if (k + 1u >= field.size())
                    {
                        error("ValueError", "end of string while looking for conversion specifier");
                        return false;
                    }
                    conversion = field[k + 1u];
                    k += 2u;
                    if (k < field.size() && field[k] != ':')
                    {
                        error("ValueError", "expected ':' after conversion specifier");
                        return false;
                    }
                }
                if (k < field.size() && field[k] == ':')
                {
                    std::string nested;
                    if (!format_string(field.substr(k + 1u), aArgs, aKwargs, nested, aDepth + 1))
                        return false;
                    spec = nested;
                }
                // (the first part of the name: an index, a keyword or automatic numbering)
                auto const first = name.find_first_of(".[");
                auto const head = name.substr(0u, first);
                ref value;
                if (head.empty() || std::all_of(head.begin(), head.end(), [](char d) { return d >= '0' && d <= '9'; }))
                {
                    std::size_t index;
                    if (head.empty())
                    {
                        if (usedManual)
                        {
                            error("ValueError", "cannot switch from manual field specification to automatic field numbering");
                            return false;
                        }
                        usedAutomatic = true;
                        index = automatic++;
                    }
                    else
                    {
                        if (usedAutomatic)
                        {
                            error("ValueError", "cannot switch from automatic field numbering to manual field specification");
                            return false;
                        }
                        usedManual = true;
                        index = static_cast<std::size_t>(std::stoull(head));
                    }
                    if (index >= aArgs.size())
                    {
                        error("IndexError", "Replacement index " + std::to_string(index) + " out of range for positional args tuple");
                        return false;
                    }
                    value = ref::borrowed(aArgs[index]);
                }
                else
                {
                    object* found = nullptr;
                    if (aKwargs != nullptr)
                        for (auto const& item : as<dict_object>(aKwargs)->items)
                            if (kind_of(item.first) == NEOS_PY_STR && as<str_object>(item.first)->value == head)
                                found = item.second;
                    if (found == nullptr)
                    {
                        error("KeyError", "'" + head + "'");
                        return false;
                    }
                    value = ref::borrowed(found);
                }
                // (.attribute and [key])
                for (std::size_t p = first; p != std::string::npos && p < name.size();)
                {
                    if (name[p] == '.')
                    {
                        auto const end = name.find_first_of(".[", p + 1u);
                        auto const attribute = name.substr(p + 1u, end == std::string::npos ? std::string::npos : end - p - 1u);
                        value = ref{ getattr(value.get(), attribute.c_str()) };
                        p = end;
                    }
                    else
                    {
                        auto const end = name.find(']', p);
                        auto const keyText = name.substr(p + 1u, end - p - 1u);
                        ref const key{ !keyText.empty() && std::all_of(keyText.begin(), keyText.end(), [](char d) { return d >= '0' && d <= '9'; }) ?
                            make_int(std::stoll(keyText)) : make_str(keyText) };
                        value = ref{ getitem(value.get(), key.get()) };
                        p = end == std::string::npos ? end : end + 1u;
                    }
                    if (!value)
                        return false;
                }
                std::string text;
                if (!convert_field(value.get(), conversion, spec, text))
                    return false;
                aResult += text;
            }
            return true;
        }

        // str % values
        object* percent_format(std::string const& aFormat, object* aValues)
        {
            std::vector<object*> values;
            bool const mapping = kind_of(aValues) == NEOS_PY_DICT;
            if (kind_of(aValues) == NEOS_PY_TUPLE)
                values = as<tuple_object>(aValues)->items;
            else
                values.push_back(aValues);
            std::size_t next = 0u;
            bool usedMapping = false;
            std::string result;
            auto const n = aFormat.size();
            auto const next_value = [&]() -> object*
            {
                if (next >= values.size())
                {
                    error("TypeError", "not enough arguments for format string");
                    return nullptr;
                }
                return values[next++];
            };
            for (std::size_t i = 0u; i < n;)
            {
                if (aFormat[i] != '%')
                {
                    result += aFormat[i++];
                    continue;
                }
                auto const start = i++;
                if (i >= n)
                    return error("ValueError", "incomplete format");
                ref keyed;
                if (aFormat[i] == '(')
                {
                    if (!mapping)
                        return error("TypeError", "format requires a mapping");
                    std::size_t depth = 1u, j = i + 1u;
                    for (; j < n && depth != 0u; ++j)
                    {
                        if (aFormat[j] == '(') ++depth;
                        else if (aFormat[j] == ')') --depth;
                    }
                    if (depth != 0u)
                        return error("ValueError", "incomplete format key");
                    ref const key{ make_str(aFormat.substr(i + 1u, j - i - 2u)) };
                    keyed = ref{ getitem(aValues, key.get()) };
                    if (!keyed)
                        return nullptr;
                    usedMapping = true;
                    i = j;
                }
                bool left = false, plus = false, space = false, alternate = false, zero = false;
                for (; i < n; ++i)
                {
                    auto const f = aFormat[i];
                    if (f == '-') left = true;
                    else if (f == '+') plus = true;
                    else if (f == ' ') space = true;
                    else if (f == '#') alternate = true;
                    else if (f == '0') zero = true;
                    else break;
                }
                long long width = -1, precision = -1;
                auto const number = [&](long long& aResult) -> bool
                {
                    if (i < n && aFormat[i] == '*')
                    {
                        ++i;
                        auto const v = next_value();
                        if (v == nullptr)
                            return false;
                        if (!is_int_like(v))
                        {
                            error("TypeError", "* wants int");
                            return false;
                        }
                        aResult = int_value(v);
                        return true;
                    }
                    if (i < n && aFormat[i] >= '0' && aFormat[i] <= '9')
                    {
                        aResult = 0;
                        while (i < n && aFormat[i] >= '0' && aFormat[i] <= '9')
                            aResult = aResult * 10 + (aFormat[i++] - '0');
                    }
                    return true;
                };
                if (!number(width))
                    return nullptr;
                if (width < -1)
                {
                    left = true;
                    width = -width;
                }
                if (i < n && aFormat[i] == '.')
                {
                    ++i;
                    precision = 0;
                    if (!number(precision))
                        return nullptr;
                }
                while (i < n && (aFormat[i] == 'h' || aFormat[i] == 'l' || aFormat[i] == 'L'))
                    ++i;
                if (i >= n)
                    return error("ValueError", "incomplete format");
                auto const type = aFormat[i++];
                if (type == '%')
                {
                    result += '%';
                    continue;
                }
                object* value = keyed ? keyed.get() : next_value();
                if (value == nullptr)
                    return nullptr;
                std::string sign, body;
                bool numeric = true;
                switch (type)
                {
                case 's':
                case 'r':
                case 'a':
                    {
                        numeric = false;
                        if (!convert_field(value, type == 's' ? 's' : type, "", body))
                            return nullptr;
                        if (precision >= 0)
                        {
                            auto const points = code_points(body);
                            std::string truncated;
                            for (std::size_t p = 0u; p < points.size() && static_cast<long long>(p) < precision; ++p)
                                truncated += points[p];
                            body = truncated;
                        }
                    }
                    break;
                case 'c':
                    {
                        numeric = false;
                        if (kind_of(value) == NEOS_PY_STR)
                        {
                            if (code_point_count(as<str_object>(value)->value) != 1u)
                                return error("TypeError", "%c requires an int or a unicode character, not a string of length " +
                                    std::to_string(code_point_count(as<str_object>(value)->value)));
                            body = as<str_object>(value)->value;
                        }
                        else
                        {
                            long long c = 0;
                            if (!to_index(value, c))
                            {
                                clear_error();
                                return error("TypeError", std::string{ "%c requires an int or a unicode character, not " } + type_name(value));
                            }
                            format_spec spec;
                            spec.type = 'c';
                            if (!format_int(c, spec, body))
                                return nullptr;
                        }
                    }
                    break;
                case 'd':
                case 'i':
                case 'u':
                case 'o':
                case 'x':
                case 'X':
                    {
                        long long v = 0;
                        std::optional<big_integer> big; // (beyond 64 bits)
                        if (is_big(value))
                            big = big_of(value);
                        else if (is_int_like(value))
                            v = int_value(value);
                        else if ((type == 'd' || type == 'i' || type == 'u') && (kind_of(value) == NEOS_PY_FLOAT || kind_of(value) == NEOS_PY_OTHER))
                        {
                            double d = 0.0;
                            if (kind_of(value) == NEOS_PY_OTHER && to_index(value, v))
                                ;
                            else
                            {
                                clear_error();
                                if (!to_double(value, d))
                                {
                                    clear_error();
                                    return error("TypeError", std::string{ "%" } + type + " format: a real number is required, not " + type_name(value));
                                }
                                if (!std::isfinite(d))
                                    return error(std::isnan(d) ? "ValueError" : "OverflowError", std::isnan(d) ? "cannot convert float NaN to integer" : "cannot convert float infinity to integer");
                                if (d >= 9223372036854775808.0 || d < -9223372036854775808.0)
                                    big = big_from_double(d);
                                else
                                    v = static_cast<long long>(std::trunc(d));
                            }
                        }
                        else if (!to_index(value, v))
                        {
                            clear_error();
                            return error("TypeError", std::string{ "%" } + type + " format: " + (type == 'd' || type == 'i' || type == 'u' ? "a real number" : "an integer") +
                                " is required, not " + type_name(value));
                        }
                        bool const negative = big ? big_negative(*big) : v < 0;
                        auto const magnitude = negative ? 0ull - static_cast<unsigned long long>(v) : static_cast<unsigned long long>(v);
                        char buffer[80];
                        auto const base = type == 'o' ? 8 : (type == 'x' || type == 'X') ? 16 : 10;
                        auto const converted = std::to_chars(buffer, buffer + sizeof(buffer), magnitude, base);
                        body.assign(buffer, converted.ptr);
                        if (big)
                        {
                            body = big_digits(*big, base);
                            if (base == 10 && sIntMaxStrDigits > 0 && body.size() > static_cast<std::size_t>(sIntMaxStrDigits))
                                return int_max_str_digits_error(body.size(), false);
                        }
                        if (type == 'X')
                            for (auto& c : body)
                                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                        if (precision >= 0 && static_cast<long long>(body.size()) < precision)
                            body.insert(0u, static_cast<std::size_t>(precision) - body.size(), '0');
                        sign = negative ? "-" : plus ? "+" : space ? " " : "";
                        if (alternate && base != 10)
                            sign += type == 'o' ? "0o" : type == 'x' ? "0x" : "0X";
                    }
                    break;
                case 'e':
                case 'E':
                case 'f':
                case 'F':
                case 'g':
                case 'G':
                    {
                        double d = 0.0;
                        if (!to_double(value, d))
                        {
                            clear_error();
                            return error("TypeError", std::string{ "must be real number, not " } + type_name(value));
                        }
                        format_spec spec;
                        spec.type = type;
                        spec.precision = precision < 0 ? 6 : precision;
                        spec.alternate = alternate;
                        std::string text;
                        if (!format_float(std::fabs(d), spec, text))
                            return nullptr;
                        body = text;
                        sign = std::signbit(d) && !std::isnan(d) ? "-" : plus ? "+" : space ? " " : "";
                    }
                    break;
                default:
                    {
                        char buffer[96];
                        std::snprintf(buffer, sizeof(buffer), "unsupported format character '%c' (0x%x) at index %zu", type, static_cast<unsigned>(static_cast<unsigned char>(type)), i - 1u);
                        (void)start;
                        return error("ValueError", buffer);
                    }
                }
                auto const length = static_cast<long long>(code_point_count(sign) + code_point_count(body));
                if (width > length)
                {
                    auto const padding = static_cast<std::size_t>(width - length);
                    if (left)
                        body = sign + body + std::string(padding, ' ');
                    else if (zero && numeric)
                        body = sign + std::string(padding, '0') + body;
                    else
                        body = std::string(padding, ' ') + sign + body;
                    sign.clear();
                }
                result += sign + body;
            }
            if (!mapping && next < values.size())
                return error("TypeError", "not all arguments converted during string formatting");
            (void)usedMapping;
            return make_str(std::move(result));
        }

        // --- functions of the program (a function used as a value): calls are made through the host

        struct script_parameter
        {
            enum kind_t { Positional, KeywordOnly, VarArgs, VarKeywords } kind = Positional;
            std::string name;
            char type = 'O';        // O(bject), I(nt, of any size), L (an int of 64 bits), F(loat), B(ool), S(tr)
            object* defaultValue = nullptr;
            bool hasDefault = false;
            bool positionalOnly = false;
        };
        struct script_function_object
        {
            header h;
            std::string name;
            unsigned index;
            std::vector<script_parameter> parameters;
            char result = 'O';      // (V: none)
            std::string qualname;
            std::string module = "__main__";    ///< (the module that defines it)
            void* program = nullptr;            ///< (the program that defines it (exec's): null for the program)
            object* globals = nullptr;          ///< (exec's: the globals of the code that defines it)
            std::vector<std::pair<std::string, std::string>> annotationSources; ///< (its annotations: evaluated when __annotations__ is first read)
            object* annotationMaker = nullptr;  ///< (a function that makes its annotations (when __annotations__ is first read): if it raises, they are their sources)
            std::vector<object*> cells;     ///< (a closure's: the values of its function's leading parameters)
            std::vector<std::pair<std::string, object*>> attributes;
            char flavour = 0;               ///< (G: a generator function, C: a coroutine function (async def), A: an async generator function)
            mutable std::int32_t directParameters = -2; ///< (the program calls it itself (see python::direct_function) with this many arguments (-1: never; -2: not yet known))
            ~script_function_object() 
            { 
                release(globals);
                release(annotationMaker);
                for (auto& p : parameters) 
                    release(p.defaultValue); 
                for (auto c : cells)
                    release(c);
                for (auto& a : attributes)
                    release(a.second);
            }
        };

        object* script_function_repr(neos_py_context*, object* aSelf)
        {
            char address[32];
            std::snprintf(address, sizeof(address), "%p", static_cast<void*>(aSelf));
            auto const& f = *as<script_function_object>(aSelf);
            return make_str("<function " + (f.qualname.empty() ? f.name : f.qualname) + " at " + address + ">");
        }

        std::map<std::string, object*>& imported();

        // --- a function's code object (__code__): what is known of its code

        struct function_code_object
        {
            header h;
            std::string name;
            std::string qualname;
            std::string filename;
            long long argcount = 0, posonlyargcount = 0, kwonlyargcount = 0, flags = 0;
            std::vector<std::string> varnames;
            std::vector<std::string> freevars;
            long long firstlineno = 1;
        };

        neos_py_type make_function_code_type()
        {
            auto type = make_type("code", "code", retain_object, release_object<function_code_object>);
            type.repr = [](neos_py_context*, object* aSelf) -> object*
            {
                auto const& c = *reinterpret_cast<function_code_object*>(aSelf);
                return make_str("<code object " + c.name + " at " + object_address(aSelf) + ", file \"" + c.filename + "\", line " + std::to_string(c.firstlineno) + ">");
            };
            type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
            {
                auto const& c = *reinterpret_cast<function_code_object*>(aSelf);
                std::string_view const name{ aName };
                auto const names = [](std::vector<std::string> const& aNames)
                {
                    std::vector<object*> items;
                    for (auto const& n : aNames)
                        items.push_back(make_str(n));
                    return make_tuple(std::move(items));
                };
                if (name == "co_name") return make_str(c.name);
                if (name == "co_qualname") return make_str(c.qualname);
                if (name == "co_filename") return make_str(c.filename);
                if (name == "co_firstlineno") return make_int(c.firstlineno);
                if (name == "co_argcount") return make_int(c.argcount);
                if (name == "co_posonlyargcount") return make_int(c.posonlyargcount);
                if (name == "co_kwonlyargcount") return make_int(c.kwonlyargcount);
                if (name == "co_flags") return make_int(c.flags);
                if (name == "co_nlocals") return make_int(static_cast<long long>(c.varnames.size()));
                if (name == "co_varnames") return names(c.varnames);
                if (name == "co_freevars") return names(c.freevars);
                if (name == "co_cellvars" || name == "co_names" || name == "co_consts") return make_tuple();
                if (name == "co_code" || name == "co_lnotab" || name == "co_linetable" || name == "co_exceptiontable") return make_bytes({});
                if (name == "co_stacksize") return make_int(0);
                return error("AttributeError", std::string{ "'code' object has no attribute '" } + aName + "'");
            };
            return type;
        }

        neos_py_type sFunctionCodeType = make_function_code_type();

        // (the namespace of a function's module (its __globals__): a new reference)
        object* function_globals(object* aFunction);

        object* function_code(object* aFunction);

        object* script_function_getattr(neos_py_context*, object* aSelf, char const* aName)
        {
            auto const& f = *as<script_function_object>(aSelf);
            for (auto const& a : f.attributes)
                if (a.first == aName)
                {
                    retain(a.second);
                    return a.second;
                }
            if (std::strcmp(aName, "__name__") == 0)
                return make_str(f.name);
            if (std::strcmp(aName, "__qualname__") == 0)
                return make_str(f.qualname.empty() ? f.name : f.qualname);
            if (std::strcmp(aName, "__module__") == 0)
                return make_str(f.module);
            if (std::strcmp(aName, "__doc__") == 0)
                return none();
            if (std::strcmp(aName, "__annotations__") == 0)
            {
                // (its annotations: evaluated (in its module's namespace) when first read)
                auto& self = *as<script_function_object>(aSelf);
                ref dict{ make_dict() };
                if (self.annotationMaker != nullptr)
                {
                    // (or the annotations themselves (a nested function's: evaluated where it is defined))
                    ref const args{ make_tuple() };
                    ref made{ kind_of(self.annotationMaker) == NEOS_PY_DICT ? (retain(self.annotationMaker), self.annotationMaker) : call(self.annotationMaker, args.get(), nullptr) };
                    if (made)
                        dict = made;
                    else
                    {
                        clear_error();
                        for (auto const& a : self.annotationSources)
                        {
                            ref const key{ make_str(a.first) };
                            ref const value{ make_str(a.second) };
                            dict_set(dict.get(), key.get(), value.get());
                        }
                    }
                    release(self.annotationMaker);
                    self.annotationMaker = nullptr;
                }
                else if (!self.annotationSources.empty())
                {
                    ref ns;
                    if (self.module == "__main__")
                        ns = ref{ fn_module_namespace("__main__", nullptr) };
                    else if (auto const m = imported().find(self.module); m != imported().end() && m->second != nullptr)
                        ns = ref{ getattr(m->second, "__dict__") };
                    if (!ns)
                    {
                        clear_error();
                        ns = ref{ make_dict() };
                    }
                    for (auto const& a : self.annotationSources)
                    {
                        ref const source{ make_str(a.second) };
                        ref value{ b_eval({ source.get(), ns.get() }, nullptr) };
                        if (!value)
                        {
                            clear_error();
                            value = ref{ make_str(a.second) };
                        }
                        ref const key{ make_str(a.first) };
                        dict_set(dict.get(), key.get(), value.get());
                    }
                }
                self.attributes.emplace_back("__annotations__", (retain(dict.get()), dict.get()));
                return (retain(dict.get()), dict.get());
            }
            if (std::strcmp(aName, "__closure__") == 0)
            {
                if (f.cells.empty())
                    return none();
                for (auto c : f.cells)
                    retain(c);
                return make_tuple(f.cells);
            }
            if (std::strcmp(aName, "__defaults__") == 0 || std::strcmp(aName, "__kwdefaults__") == 0)
            {
                bool const keywords = aName[2] == 'k';
                std::vector<object*> values;
                object* kw = nullptr;
                for (auto const& p : f.parameters)
                    if (p.defaultValue != nullptr)
                    {
                        if (p.kind == script_parameter::KeywordOnly)
                        {
                            if (kw == nullptr)
                                kw = make_dict();
                            ref const key{ make_str(p.name) };
                            dict_set(kw, key.get(), p.defaultValue);
                        }
                        else
                        {
                            retain(p.defaultValue);
                            values.push_back(p.defaultValue);
                        }
                    }
                if (keywords)
                {
                    for (auto v : values)
                        release(v);
                    return kw != nullptr ? kw : none();
                }
                release(kw);
                return values.empty() ? none() : make_tuple(std::move(values));
            }
            if (std::strcmp(aName, "__dict__") == 0)
            {
                auto const d = make_dict();
                for (auto const& a : f.attributes)
                {
                    if (a.first == "__doc__" || a.first == "__annotations__" || a.first == "__type_params__")
                        continue;
                    ref const key{ make_str(a.first) };
                    dict_set(d, key.get(), a.second);
                }
                return d;
            }
            if (std::strcmp(aName, "__code__") == 0)
                return function_code(aSelf);
            if (std::strcmp(aName, "__globals__") == 0)
                return function_globals(aSelf);
            if (std::strcmp(aName, "__type_params__") == 0)
                return make_tuple();
            if (std::strcmp(aName, "__builtins__") == 0)
                return getattr(ref{ fn_import("builtins") }.get(), "__dict__");
            return error("AttributeError", "'function' object has no attribute '" + std::string{ aName } + "'");
        }

        int script_function_setattr(neos_py_context*, object* aSelf, char const* aName, object* aValue)
        {
            auto& f = *as<script_function_object>(aSelf);
            if ((std::strcmp(aName, "__name__") == 0 || std::strcmp(aName, "__qualname__") == 0) && aValue != nullptr)
            {
                if (kind_of(aValue) != NEOS_PY_STR)
                {
                    error("TypeError", std::string{ aName } + " must be set to a string object");
                    return -1;
                }
                (aName[2] == 'n' ? f.name : f.qualname) = as<str_object>(aValue)->value;
                return 0;
            }
            for (auto a = f.attributes.begin(); a != f.attributes.end(); ++a)
                if (a->first == aName)
                {
                    release(a->second);
                    if (aValue == nullptr)
                        f.attributes.erase(a);
                    else
                    {
                        retain(aValue);
                        a->second = aValue;
                    }
                    return 0;
                }
            if (aValue == nullptr)
            {
                error("AttributeError", "'function' object has no attribute '" + std::string{ aName } + "'");
                return -1;
            }
            retain(aValue);
            f.attributes.emplace_back(aName, aValue);
            return 0;
        }

        object* script_function_invoke(object* aFunction, object* aFirst, std::span<object* const> aArgs, object* aKwargs);
        struct script_function_object;
        object* script_function_run(script_function_object const& f, std::size_t aCount, neos_rt_type const* aTypes, neos_rt_value const* aValues);

        object* script_function_call(neos_py_context*, object* aSelf, object* aArgs, object* aKwargs)
        {
            return script_function_invoke(aSelf, nullptr, tuple_items(aArgs), aKwargs);
        }

        // (a call of a function of the program: its arguments aFirst (if not null: e.g. a method's instance) then aArgs)
        object* script_function_invoke(object* aFunction, object* aFirst, std::span<object* const> aArgs, object* aKwargs)
        {
            auto const& f = *as<script_function_object>(aFunction);
            // (the usual call: an object for each of its parameters, which are positional parameters of objects (no keywords, no closure): 
            // as they are)
            if (auto const count = aArgs.size() + (aFirst != nullptr ? 1u : 0u); count <= 8u && count == f.parameters.size() && f.cells.empty() &&
                (aKwargs == nullptr || as<dict_object>(aKwargs)->items.empty()) && 
                std::all_of(f.parameters.begin(), f.parameters.end(), [](script_parameter const& aParameter) { return aParameter.kind == script_parameter::Positional && aParameter.type == 'O'; }))
            {
                neos_rt_type types[8];
                neos_rt_value values[8] = {};
                std::size_t i = 0u;
                if (aFirst != nullptr)
                {
                    types[i] = NEOS_RT_OBJECT;
                    values[i++].object = aFirst;
                }
                for (auto a : aArgs)
                {
                    types[i] = NEOS_RT_OBJECT;
                    values[i++].object = a;
                }
                return script_function_run(f, count, types, values);
            }
            neonumeric::vecarray<object*, 8u> args;
            if (aFirst != nullptr)
                args.push_back(aFirst);
            args.insert(args.end(), aArgs.begin(), aArgs.end());
            auto const& parameters = f.parameters;
            // (a call of a few parameters: on the stack)
            neonumeric::vecarray<object*, 8u> bound(parameters.size(), nullptr);
            std::vector<ref> owned;
            std::vector<object*> extra;
            std::vector<std::pair<object*, object*>> extraKeywords;
            std::size_t positional = 0u, required = 0u;
            std::optional<std::size_t> varArgs, varKeywords;
            for (std::size_t p = 0u; p < parameters.size(); ++p)
            {
                if (parameters[p].kind == script_parameter::Positional)
                {
                    ++positional;
                    if (parameters[p].defaultValue == nullptr)
                        ++required;
                }
                else if (parameters[p].kind == script_parameter::VarArgs)
                    varArgs = p;
                else if (parameters[p].kind == script_parameter::VarKeywords)
                    varKeywords = p;
            }
            std::size_t next = 0u;
            for (auto a : args)
            {
                if (next < parameters.size() && parameters[next].kind == script_parameter::Positional)
                    bound[next++] = a;
                else if (varArgs)
                    extra.push_back(a);
                else
                {
                    std::string const takes = required == positional ? std::to_string(positional) : "from " + std::to_string(required) + " to " + std::to_string(positional);
                    return error("TypeError", f.name + "() takes " + takes + " positional argument" + (positional == 1u && required == positional ? "" : "s") + " but " +
                        std::to_string(args.size()) + (args.size() == 1u ? " was" : " were") + " given");
                }
            }
            if (aKwargs != nullptr)
                for (auto const& item : as<dict_object>(aKwargs)->items)
                {
                    auto const& key = as<str_object>(item.first)->value;
                    std::optional<std::size_t> slot;
                    for (std::size_t p = 0u; p < parameters.size(); ++p)
                        if ((parameters[p].kind == script_parameter::Positional || parameters[p].kind == script_parameter::KeywordOnly) && parameters[p].name == key &&
                            !parameters[p].positionalOnly)
                            slot = p;
                    if (!slot && !varKeywords)
                        for (auto const& p : parameters)
                            if (p.positionalOnly && p.name == key)
                                return error("TypeError", f.name + "() got some positional-only arguments passed as keyword arguments: '" + key + "'");
                    if (slot)
                    {
                        if (bound[*slot] != nullptr)
                            return error("TypeError", f.name + "() got multiple values for argument '" + key + "'");
                        bound[*slot] = item.second;
                    }
                    else if (varKeywords)
                        extraKeywords.push_back(item);
                    else
                        return error("TypeError", f.name + "() got an unexpected keyword argument '" + key + "'");
                }
            std::vector<std::string> missing;
            bool missingKeywordOnly = false;
            for (std::size_t p = 0u; p < parameters.size(); ++p)
            {
                auto const& parameter = parameters[p];
                if (parameter.kind == script_parameter::VarArgs)
                {
                    for (auto x : extra)
                        retain(x);
                    owned.emplace_back(make_tuple(extra));
                    bound[p] = owned.back().get();
                }
                else if (parameter.kind == script_parameter::VarKeywords)
                {
                    owned.emplace_back(make_dict());
                    for (auto const& item : extraKeywords)
                        dict_set(owned.back().get(), item.first, item.second);
                    bound[p] = owned.back().get();
                }
                else if (bound[p] == nullptr)
                {
                    if (parameter.defaultValue != nullptr)
                        bound[p] = parameter.defaultValue;
                    else
                    {
                        missing.push_back(parameter.name);
                        missingKeywordOnly = parameter.kind == script_parameter::KeywordOnly;
                    }
                }
            }
            if (!missing.empty())
            {
                std::string names;
                for (std::size_t i = 0u; i < missing.size(); ++i)
                    names += (i == 0u ? "'" : i + 1u == missing.size() ? (missing.size() > 2u ? "', and '" : "' and '") : "', '") + missing[i];
                return error("TypeError", f.name + "() missing " + std::to_string(missing.size()) + " required " + (missingKeywordOnly ? "keyword-only" : "positional") +
                    " argument" + (missing.size() == 1u ? "" : "s") + ": " + names + "'");
            }
            // (the arguments as the function's parameter types; a closure's cells first)
            neonumeric::vecarray<neos_rt_type, 8u> types;
            neonumeric::vecarray<neos_rt_value, 8u> values;
            std::vector<std::string> strings;
            if (std::any_of(parameters.begin(), parameters.end(), [](auto const& aParameter) { return aParameter.type == 'S'; }))
                strings.reserve(parameters.size());     // (their texts do not move)
            for (auto c : f.cells)
            {
                neos_rt_value v{};
                v.object = c;
                types.push_back(NEOS_RT_OBJECT);
                values.push_back(v);
            }
            for (std::size_t p = 0u; p < parameters.size(); ++p)
            {
                neos_rt_value v{};
                switch (parameters[p].type)
                {
                case 'I':
                    {
                        // (an int of any size: borrowed)
                        if (!is_int_like(bound[p]))
                        {
                            long long i = 0;
                            if (!to_index(bound[p], i))
                                return nullptr;
                            owned.emplace_back(make_int(i));
                            bound[p] = owned.back().get();
                        }
                        if (kind_of(bound[p]) == NEOS_PY_BOOL)
                            v.i64 = small_integer(bound[p] == &sTrue.base ? 1 : 0);
                        else if (!is_big(bound[p]) && as<int_object>(bound[p])->value >= -sSmallIntegerLimit && as<int_object>(bound[p])->value < sSmallIntegerLimit)
                            v.i64 = small_integer(as<int_object>(bound[p])->value);
                        else
                            v.i64 = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(bound[p]) | 1u);
                        types.push_back(NEOS_RT_INTEGER);
                    }
                    break;
                case 'L':
                    {
                        // (an int of 64 bits)
                        long long i = 0;
                        if (!to_index(bound[p], i))
                            return nullptr;
                        v.i64 = i;
                        types.push_back(NEOS_RT_I64);
                    }
                    break;
                case 'F':
                    if (!to_double(bound[p], v.f64))
                        return nullptr;
                    types.push_back(NEOS_RT_F64);
                    break;
                case 'B':
                    {
                        auto const t = truth(bound[p]);
                        if (t < 0)
                            return nullptr;
                        v.b = t;
                        types.push_back(NEOS_RT_BOOL);
                    }
                    break;
                case 'S':
                    strings.emplace_back();
                    if (!str_of(bound[p], strings.back()))
                        return nullptr;
                    v.string = strings.back().c_str();
                    types.push_back(NEOS_RT_STRING);
                    break;
                default:
                    v.object = bound[p];
                    types.push_back(NEOS_RT_OBJECT);
                    break;
                }
                values.push_back(v);
            }
            return script_function_run(f, values.size(), types.data(), values.data());
        }

        // (a function of the program called with its arguments as the host takes them)
        object* script_function_run(script_function_object const& f, std::size_t aCount, neos_rt_type const* aTypes, neos_rt_value const* aValues)
        {
            auto const* host = python_runtime::host();
            if (host == nullptr)
                return error("SystemError", "functions of the program cannot be called");
            neos_rt_type const resultType = f.result == 'I' ? NEOS_RT_INTEGER : f.result == 'L' ? NEOS_RT_I64 : f.result == 'F' ? NEOS_RT_F64 : f.result == 'B' ? NEOS_RT_BOOL :
                f.result == 'S' ? NEOS_RT_STRING : f.result == 'V' ? NEOS_RT_VOID : NEOS_RT_OBJECT;
            neos_rt_value r{};
            // (a function of a program that exec loaded: called on its program)
            struct program_switch
            {
                void* previous;
                bool switched;
                bool scoped;
                explicit program_switch(script_function_object const& aFunction) : 
                    previous{ current_program() }, switched{ aFunction.program != previous }, scoped{ aFunction.program != nullptr }
                { 
                    if (switched) 
                        switch_program(aFunction.program); 
                    // (exec's: its globals)
                    if (scoped)
                        exec_enter(aFunction.globals);
                }
                ~program_switch() 
                { 
                    if (scoped)
                        exec_leave();
                    if (switched) 
                        switch_program(previous); 
                }
            } const switched{ f };
            if (auto const message = host->call(f.index, static_cast<unsigned>(aCount), aTypes, aValues, resultType, &r))
            {
                std::string const text = message;
                auto const colon = text.find(": ");
                if (colon == std::string::npos)
                    python_runtime::raise(text, "");
                else
                    python_runtime::raise(text.substr(0u, colon), text.substr(colon + 2u));
                return nullptr;
            }
            switch (f.result)
            {
            case 'I': return integer_is_object(r.i64) ? integer_object(r.i64) : make_int(r.i64 >> 1);
            case 'L': return make_int(r.i64);
            case 'F': return make_float(r.f64);
            case 'B': return make_bool(r.b != 0);
            case 'S': return make_str(r.string != nullptr ? r.string : "");
            case 'V': return none();
            default: return r.object != nullptr ? r.object : none();
            }
        }

        neos_py_type make_script_function_type()
        {
            auto type = make_type("python.function", "function", retain_object, release_object<script_function_object>);
            type.repr = script_function_repr;
            type.getattr = script_function_getattr;
            type.setattr = script_function_setattr;
            type.call = script_function_call;
            return type;
        }

        neos_py_type sScriptFunctionType = make_script_function_type();

        // python::function(name, index, signature, defaults): signature: "[*|**]name:T[=],...;R" (T: O, I, L, F, B or S; R also V)
        // --- cells (a function's variables that the functions nested in it use: "x" is "_k_x.v") and closures

        std::string object_address(void const* aObject);

        struct cell_object
        {
            header h;
            std::string name;
            object* value = nullptr;
            ~cell_object() { release(value); }
        };

        neos_py_type make_cell_type()
        {
            auto type = make_type("python.cell", "cell", retain_object, release_object<cell_object>);
            type.repr = [](neos_py_context*, object* aSelf) -> object*
            {
                auto const& c = *as<cell_object>(aSelf);
                return make_str("<cell at " + object_address(aSelf) + ": " + (c.value == nullptr ? std::string{ "empty" } :
                    std::string{ type_name(c.value) } + " object at " + object_address(c.value)) + ">");
            };
            type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
            {
                auto const& c = *as<cell_object>(aSelf);
                if (std::strcmp(aName, "v") == 0 || std::strcmp(aName, "cell_contents") == 0)
                {
                    if (c.value == nullptr)
                        return aName[0] == 'v' ? error("NameError", "cannot access free variable '" + c.name + "' where it is not associated with a value in enclosing scope") :
                            error("ValueError", "Cell is empty");
                    retain(c.value);
                    return c.value;
                }
                return error("AttributeError", "'cell' object has no attribute '" + std::string{ aName } + "'");
            };
            type.setattr = [](neos_py_context*, object* aSelf, char const* aName, object* aValue) -> int
            {
                auto& c = *as<cell_object>(aSelf);
                if (std::strcmp(aName, "v") != 0 && std::strcmp(aName, "cell_contents") != 0)
                {
                    error("AttributeError", "'cell' object has no attribute '" + std::string{ aName } + "'");
                    return -1;
                }
                if (aValue == nullptr && c.value == nullptr)
                {
                    error("NameError", "cannot access free variable '" + c.name + "' where it is not associated with a value in enclosing scope");
                    return -1;
                }
                retain(aValue);
                release(c.value);
                c.value = aValue;
                return 0;
            };
            return type;
        }

        neos_py_type sCellType = make_cell_type();

        // __cell(name[, value])
        object* b_cell(std::vector<object*> const& aArgs, object*)
        {
            auto const c = new cell_object{ { { &sCellType.base } }, aArgs.empty() || kind_of(aArgs[0]) != NEOS_PY_STR ? std::string{} : as<str_object>(aArgs[0])->value };
            if (aArgs.size() > 1u)
            {
                retain(aArgs[1]);
                c->value = aArgs[1];
            }
            return &c->h.base;
        }

        // __closure(function, name, qualname, defaults, cells...): a function whose leading parameters are the cells (and whose 
        // default values are those given (a tuple), if any)
        object* b_closure(std::vector<object*> const& aArgs, object*)
        {
            if (aArgs.size() < 4u || aArgs[0]->type != &sScriptFunctionType.base)
                return error("SystemError", "__closure(function, name, qualname, defaults, cells...)");
            auto const& function = *as<script_function_object>(aArgs[0]);
            auto const cells = aArgs.size() - 4u;
            if (function.parameters.size() < cells)
                return error("SystemError", "__closure: too few parameters");
            auto* const f = new script_function_object{ { { &sScriptFunctionType.base } }, as<str_object>(aArgs[1])->value, function.index, {}, function.result };
            f->qualname = as<str_object>(aArgs[2])->value;
            f->module = function.module;
            f->program = function.program;
            f->globals = function.globals;
            retain(f->globals);
            f->flavour = function.flavour;
            f->parameters.assign(function.parameters.begin() + static_cast<std::ptrdiff_t>(cells), function.parameters.end());
            std::vector<object*> const* defaults = kind_of(aArgs[3]) == NEOS_PY_TUPLE ? &tuple_items(aArgs[3]) : nullptr;
            std::size_t next = 0u;
            for (auto& p : f->parameters)
            {
                if (p.hasDefault && defaults != nullptr && next < defaults->size())
                    p.defaultValue = (*defaults)[next++];
                retain(p.defaultValue);
            }
            for (std::size_t i = 4u; i < aArgs.size(); ++i)
            {
                retain(aArgs[i]);
                f->cells.push_back(aArgs[i]);
            }
            // (its function's attributes (e.g. its docstring))
            for (auto const& a : function.attributes)
                f->attributes.emplace_back(a.first, (retain(a.second), a.second));
            return &f->h.base;
        }

        object* make_script_function(char const* aName, std::int32_t aIndex, char const* aSignature, object* aDefaults);

        object* function_globals(object* aFunction)
        {
            auto const& f = *as<script_function_object>(aFunction);
            if (f.globals != nullptr)
                return new_reference(f.globals);
            if (f.module != "__main__")
                if (auto const m = imported().find(f.module); m != imported().end() && m->second != nullptr)
                    return getattr(m->second, "__dict__");
            return fn_module_namespace("__main__", nullptr);
        }

        object* function_code(object* aFunction)
        {
            auto const& f = *as<script_function_object>(aFunction);
            auto* const c = new function_code_object{ { { &sFunctionCodeType.base } }, f.name, f.qualname.empty() ? f.name : f.qualname };
            ref result{ &c->h.base };
            // (its module's file)
            if (ref const g{ function_globals(aFunction) }; g && kind_of(g.get()) == NEOS_PY_DICT)
            {
                for (auto const& item : as<dict_object>(g.get())->items)
                    if (kind_of(item.first) == NEOS_PY_STR && as<str_object>(item.first)->value == "__file__" && kind_of(item.second) == NEOS_PY_STR)
                        c->filename = as<str_object>(item.second)->value;
            }
            clear_error();
            // (its source and first line: its frame's, as the host knows its function)
            if (auto const* h = host(); h != nullptr && h->version >= 6u && h->function_frame != nullptr)
            {
                neos_rt_frame frame{};
                if (h->function_frame(program_context(f.program), f.index, &frame) != 0u)
                {
                    if (frame.source != nullptr && *frame.source != '\0')
                        c->filename = frame.source;
                    if (frame.firstLine != 0u)
                        c->firstlineno = frame.firstLine;
                }
            }
            // (CO_OPTIMIZED | CO_NEWLOCALS; CO_VARARGS, CO_VARKEYWORDS, CO_NESTED; CO_GENERATOR, CO_COROUTINE, CO_ASYNC_GENERATOR)
            c->flags = 0x3;
            std::vector<std::string> keywordOnly, var;
            for (auto const& p : f.parameters)
                switch (p.kind)
                {
                case script_parameter::Positional:
                    ++c->argcount;
                    if (p.positionalOnly)
                        ++c->posonlyargcount;
                    c->varnames.push_back(p.name);
                    break;
                case script_parameter::KeywordOnly:
                    ++c->kwonlyargcount;
                    keywordOnly.push_back(p.name);
                    break;
                case script_parameter::VarArgs:
                    c->flags |= 0x4;
                    var.insert(var.begin(), p.name);
                    break;
                case script_parameter::VarKeywords:
                    c->flags |= 0x8;
                    var.push_back(p.name);
                    break;
                }
            c->varnames.insert(c->varnames.end(), keywordOnly.begin(), keywordOnly.end());
            c->varnames.insert(c->varnames.end(), var.begin(), var.end());
            if (!f.cells.empty())
                c->flags |= 0x10;
            for (auto cell : f.cells)
                if (cell != nullptr && cell->type == &sCellType.base)
                {
                    auto const& n = as<cell_object>(cell)->name;
                    c->freevars.push_back(n.starts_with("_k_") ? n.substr(3u) : n);
                }
            if (f.flavour == 'G') c->flags |= 0x20;
            if (f.flavour == 'C') c->flags |= 0x80;
            if (f.flavour == 'A') c->flags |= 0x200;
            return result.release_ownership();
        }

        // (a function (def): the same object for each reference to it (its identity), unless its defaults differ)
        bool set_function_annotations(object* aFunction, object* aMaker, object* aSources)
        {
            if (aFunction == nullptr || aFunction->type != &sScriptFunctionType.base)
                return setattr(aFunction, "__annotations__", aMaker) >= 0;
            auto& f = *as<script_function_object>(aFunction);
            retain(aMaker);
            release(f.annotationMaker);
            f.annotationMaker = aMaker;
            f.annotationSources.clear();
            auto const& sources = tuple_items(aSources);
            for (std::size_t i = 0u; i + 1u < sources.size(); i += 2u)
                if (kind_of(sources[i]) == NEOS_PY_STR && kind_of(sources[i + 1u]) == NEOS_PY_STR)
                    f.annotationSources.emplace_back(as<str_object>(sources[i])->value, as<str_object>(sources[i + 1u])->value);
            return true;
        }

        object* fn_function(char const* aName, std::int32_t aIndex, char const* aSignature, object* aDefaults)
        {
            static std::map<std::tuple<void*, object*, std::int32_t>, object*> sFunctions;
            auto& cached = sFunctions[{ current_program(), current_program() != nullptr ? exec_current_globals() : nullptr, aIndex }];
            if (cached != nullptr)
            {
                auto const& f = *as<script_function_object>(cached);
                auto const& defaults = aDefaults != nullptr && kind_of(aDefaults) == NEOS_PY_TUPLE ? tuple_items(aDefaults) : std::vector<object*>{};
                std::size_t next = 0u;
                bool same = true;
                for (auto const& p : f.parameters)
                    if (p.hasDefault)
                        same = same && next < defaults.size() && defaults[next++] == p.defaultValue;
                if (same && next == defaults.size())
                {
                    retain(cached);
                    return cached;
                }
                release(cached);
            }
            cached = make_script_function(aName, aIndex, aSignature, aDefaults);
            retain(cached);
            return cached;
        }

        // (the modules of the program's source files: by their numbers (a function of module N is named _MN_name: see the
        // Python compiler's modules))
        std::map<long long, std::string>& source_modules()
        {
            static std::map<long long, std::string> sModules;
            return sModules;
        }

        // (a function's name: its module's prefix (_MN_) removed (aModule: its module))
        std::string unprefixed_name(std::string_view aName, std::string& aModule)
        {
            aModule = "__main__";
            if (aName.size() > 4u && aName[0] == '_' && aName[1] == 'M' && std::isdigit(static_cast<unsigned char>(aName[2])))
            {
                auto const underscore = aName.find('_', 2u);
                if (underscore != std::string_view::npos && underscore + 1u < aName.size())
                {
                    long long n = 0;
                    for (auto i = 2u; i < underscore; ++i)
                    {
                        if (!std::isdigit(static_cast<unsigned char>(aName[i])))
                            return std::string{ aName };
                        n = n * 10 + (aName[i] - '0');
                    }
                    if (auto const m = source_modules().find(n); m != source_modules().end())
                        aModule = m->second;
                    return std::string{ aName.substr(underscore + 1u) };
                }
            }
            return std::string{ aName };
        }

        object* make_script_function(char const* aName, std::int32_t aIndex, char const* aSignature, object* aDefaults)
        {
            std::string module;
            auto const name = unprefixed_name(aName, module);
            auto* const f = new script_function_object{ { { &sScriptFunctionType.base } }, name, static_cast<unsigned>(aIndex), {}, 'O' };
            f->module = module;
            f->program = current_program();
            if (f->program != nullptr)
            {
                f->globals = exec_current_globals();
                retain(f->globals);
            }
            std::string_view signature{ aSignature };
            // (its docstring: after a record separator)
            if (auto const separator = signature.find('\x1e'); separator != std::string_view::npos)
            {
                f->attributes.emplace_back("__doc__", make_str(std::string{ signature.substr(separator + 1u) }));
                signature = signature.substr(0u, separator);
            }
            // (its annotations' sources: after group separators (name, unit separator, source))
            if (auto const separator = signature.find('\x1d'); separator != std::string_view::npos)
            {
                auto rest = signature.substr(separator + 1u);
                signature = signature.substr(0u, separator);
                for (;;)
                {
                    auto const next = rest.find('\x1d');
                    auto const entry = rest.substr(0u, next);
                    auto const unit = entry.find('\x1f');
                    if (unit != std::string_view::npos)
                        f->annotationSources.emplace_back(std::string{ entry.substr(0u, unit) }, std::string{ entry.substr(unit + 1u) });
                    if (next == std::string_view::npos)
                        break;
                    rest = rest.substr(next + 1u);
                }
            }
            // (a generator function, a coroutine function or an async generator function: after a file separator)
            if (auto const separator = signature.find('\x1c'); separator != std::string_view::npos)
            {
                if (separator + 1u < signature.size())
                    f->flavour = signature[separator + 1u];
                signature = signature.substr(0u, separator);
            }
            auto const semicolon = signature.rfind(';');
            auto const list = signature.substr(0u, semicolon);
            if (semicolon != std::string_view::npos && semicolon + 1u < signature.size())
                f->result = signature[semicolon + 1u];
            std::size_t nextDefault = 0u;
            auto const& defaults = aDefaults != nullptr && kind_of(aDefaults) == NEOS_PY_TUPLE ? tuple_items(aDefaults) : std::vector<object*>{};
            bool keywordOnly = false;
            std::size_t start = 0u;
            while (start < list.size())
            {
                auto end = list.find(',', start);
                if (end == std::string_view::npos)
                    end = list.size();
                auto item = list.substr(start, end - start);
                start = end + 1u;
                if (item == "*")
                {
                    keywordOnly = true;
                    continue;
                }
                if (item == "/")
                {
                    // (the parameters before it are positional-only)
                    for (auto& previous : f->parameters)
                        previous.positionalOnly = true;
                    continue;
                }
                script_parameter p;
                if (item.starts_with("**"))
                {
                    p.kind = script_parameter::VarKeywords;
                    item.remove_prefix(2u);
                }
                else if (item.starts_with("*"))
                {
                    p.kind = script_parameter::VarArgs;
                    keywordOnly = true;
                    item.remove_prefix(1u);
                }
                else if (keywordOnly)
                    p.kind = script_parameter::KeywordOnly;
                bool const hasDefault = item.ends_with("=");
                if (hasDefault)
                    item.remove_suffix(1u);
                auto const colon = item.find(':');
                p.name = std::string{ item.substr(0u, colon) };
                if (colon != std::string_view::npos && colon + 1u < item.size())
                    p.type = item[colon + 1u];
                p.hasDefault = hasDefault;
                if (hasDefault && nextDefault < defaults.size())
                {
                    p.defaultValue = defaults[nextDefault++];
                    retain(p.defaultValue);
                }
                f->parameters.push_back(p);
            }
            return &f->h.base;
        }

        // --- the context

        void ctx_raise(neos_py_context*, char const* aType, char const* aMessage) { python_runtime::raise(aType, aMessage); }
        int ctx_failed(neos_py_context*) { return failed() ? 1 : 0; }
        object* ctx_none(neos_py_context*) { return none(); }
        object* ctx_not_implemented(neos_py_context*) { return not_implemented(); }
        object* ctx_ellipsis(neos_py_context*) { return &sEllipsis.base; }
        object* ctx_from_bool(neos_py_context*, int aValue) { return make_bool(aValue != 0); }
        object* ctx_from_int(neos_py_context*, long long aValue) { return make_int(aValue); }
        object* ctx_from_float(neos_py_context*, double aValue) { return make_float(aValue); }
        object* ctx_from_complex(neos_py_context*, double aReal, double aImag) { return make_complex(aReal, aImag); }
        object* ctx_from_str(neos_py_context*, char const* aText, std::size_t aLength) { return make_str(std::string{ aText, aLength }); }
        int ctx_kind(neos_py_context*, object* aObject) { return kind_of(aObject); }
        char const* ctx_type_name(neos_py_context*, object* aObject) { return type_name(aObject); }
        int ctx_as_int(neos_py_context*, object* aObject, long long* aResult) { return to_index(aObject, *aResult) ? 0 : -1; }
        int ctx_as_float(neos_py_context*, object* aObject, double* aResult) { return to_double(aObject, *aResult) ? 0 : -1; }
        int ctx_as_complex(neos_py_context*, object* aObject, double* aReal, double* aImag)
        {
            if (kind_of(aObject) == NEOS_PY_COMPLEX)
            {
                *aReal = as<complex_object>(aObject)->real;
                *aImag = as<complex_object>(aObject)->imag;
                return 0;
            }
            *aImag = 0.0;
            return to_double(aObject, *aReal) ? 0 : -1;
        }
        char const* ctx_as_str(neos_py_context*, object* aObject, std::size_t* aLength)
        {
            if (kind_of(aObject) != NEOS_PY_STR)
                return nullptr;
            if (aLength != nullptr)
                *aLength = as<str_object>(aObject)->value.size();
            return as<str_object>(aObject)->value.c_str();
        }
        object* ctx_tuple_new(neos_py_context*, std::size_t aSize)
        {
            std::vector<object*> items(aSize, none());
            return make_tuple(std::move(items));
        }
        void ctx_tuple_set(neos_py_context*, object* aTuple, std::size_t aIndex, object* aItem)
        {
            auto& items = as<tuple_object>(aTuple)->items;
            release(items[aIndex]);
            items[aIndex] = aItem;
        }
        object* ctx_list_new(neos_py_context*) { return make_list(); }
        int ctx_list_append(neos_py_context*, object* aList, object* aItem) { retain(aItem); as<list_object>(aList)->items.push_back(aItem); return 0; }
        std::size_t ctx_sequence_size(neos_py_context*, object* aObject)
        {
            auto const k = kind_of(aObject);
            return k == NEOS_PY_LIST ? as<list_object>(aObject)->items.size() : k == NEOS_PY_TUPLE ? as<tuple_object>(aObject)->items.size() : 0u;
        }
        object* ctx_sequence_item(neos_py_context*, object* aObject, std::size_t aIndex)
        {
            return kind_of(aObject) == NEOS_PY_LIST ? as<list_object>(aObject)->items[aIndex] : as<tuple_object>(aObject)->items[aIndex];
        }
        object* ctx_dict_new(neos_py_context*) { return make_dict(); }
        int ctx_dict_set(neos_py_context*, object* aDict, object* aKey, object* aValue) { return dict_set(aDict, aKey, aValue); }
        std::size_t ctx_dict_size(neos_py_context*, object* aDict) { return aDict == nullptr ? 0u : as<dict_object>(aDict)->items.size(); }
        void ctx_dict_item(neos_py_context*, object* aDict, std::size_t aIndex, object** aKey, object** aValue)
        {
            auto const& item = as<dict_object>(aDict)->items[aIndex];
            *aKey = item.first;
            *aValue = item.second;
        }
        object* ctx_dict_get(neos_py_context*, object* aDict, char const* aKey)
        {
            if (aDict == nullptr)
                return nullptr;
            for (auto const& item : as<dict_object>(aDict)->items)
                if (kind_of(item.first) == NEOS_PY_STR && as<str_object>(item.first)->value == aKey)
                    return item.second;
            return nullptr;
        }
        void ctx_slice_parts(neos_py_context*, object* aSlice, object** aStart, object** aStop, object** aStep)
        {
            auto const& s = *as<slice_object>(aSlice);
            *aStart = s.start;
            *aStop = s.stop;
            *aStep = s.step;
        }
        object* ctx_slice_new(neos_py_context*, object* aStart, object* aStop, object* aStep) { return make_slice(aStart, aStop, aStep); }
        object* ctx_function_new(neos_py_context*, char const* aName, native_function aFunction, void* aData) { return make_function(aName, aFunction, aData); }
        object* ctx_repr(neos_py_context*, object* aObject)
        {
            std::string text;
            return repr_of(aObject, text) ? make_str(std::move(text)) : nullptr;
        }
        object* ctx_str(neos_py_context*, object* aObject)
        {
            std::string text;
            return str_of(aObject, text) ? make_str(std::move(text)) : nullptr;
        }
        object* ctx_getattr(neos_py_context*, object* aObject, char const* aName) { return getattr(aObject, aName); }
        object* ctx_call(neos_py_context*, object* aCallable, object* aArgs, object* aKwargs) { return call(aCallable, aArgs, aKwargs); }
        object* ctx_getitem(neos_py_context*, object* aObject, object* aKey) { return getitem(aObject, aKey); }
        object* ctx_binary(neos_py_context*, int aOperation, object* aLhs, object* aRhs) { return binary(aOperation, aLhs, aRhs, false); }
        object* ctx_unary(neos_py_context*, int aOperation, object* aObject) { return unary(aOperation, aObject); }
        object* ctx_compare(neos_py_context*, int aOperation, object* aLhs, object* aRhs) { return compare(aOperation, aLhs, aRhs); }
        int ctx_truth(neos_py_context*, object* aObject) { return truth(aObject); }
        long long ctx_length(neos_py_context*, object* aObject) { return length(aObject); }
        object* ctx_iter(neos_py_context*, object* aObject) { return iter(aObject); }
        object* ctx_next(neos_py_context*, object* aIterator) { return next(aIterator); }

        object* ctx_method_new(neos_py_context*, char const* aName, native_function aFunction, void* aData, object* aSelf) { return make_function(aName, aFunction, aData, aSelf); }
        int ctx_setitem(neos_py_context*, object* aObject, object* aKey, object* aValue) { return setitem(aObject, aKey, aValue); }
        int ctx_setattr(neos_py_context*, object* aObject, char const* aName, object* aValue) { return setattr(aObject, aName, aValue); }
        object* ctx_type_new(neos_py_context*, char const* aName, char const* aQualifiedName, native_function aCall, void* aData, 
            int (*aInstancecheck)(neos_py_context*, void*, object*));

        char const* ctx_exception(neos_py_context*) { return failed() ? python_runtime::error_text() : nullptr; }
        void ctx_clear(neos_py_context*) { clear_error(); }

        neos_py_context sContext
        {
            NEOS_PY_PROTOCOL_VERSION,
            ctx_raise, ctx_failed,
            ctx_none, ctx_not_implemented, ctx_ellipsis, ctx_from_bool, ctx_from_int, ctx_from_float, ctx_from_complex, ctx_from_str,
            ctx_kind, ctx_type_name, ctx_as_int, ctx_as_float, ctx_as_complex, ctx_as_str,
            ctx_tuple_new, ctx_tuple_set, ctx_list_new, ctx_list_append, ctx_sequence_size, ctx_sequence_item,
            ctx_dict_new, ctx_dict_set, ctx_dict_size, ctx_dict_item, ctx_dict_get, ctx_slice_parts, ctx_slice_new, ctx_function_new,
            ctx_repr, ctx_str, ctx_getattr, ctx_call, ctx_getitem, ctx_binary, ctx_unary, ctx_compare, ctx_truth, ctx_length,
            ctx_iter, ctx_next, retain, release, ctx_method_new, ctx_setitem, ctx_setattr, ctx_type_new, ctx_exception, ctx_clear
        };

        neos_py_context* context() { return &sContext; }

        neos_rt_object_type const sContextType{ NEOS_PY_CONTEXT_TYPE_NAME, retain_static, release_static };
        neos_py_context_object sContextObject{ { &sContextType }, &sContext };

        // --- the functions scripts call (an exception stops the script: see python.cpp's plugin error)

        // (a result that is a new reference, or null if an exception was raised (its test apart: a result is the common case))
#if defined(_MSC_VER)
        __declspec(noinline)
#else
        __attribute__((noinline))
#endif
        void null_result()
        {
            if (!failed())
                python_runtime::raise("SystemError", "error return without exception set");
        }
        inline object* checked(object* aResult)
        {
            if (aResult == nullptr)
                null_result();
            return aResult;
        }

        object* fn_context() { return &sContextObject.base; }
        object* fn_none() { return none(); }
        object* fn_ellipsis() { return &sEllipsis.base; }
        object* fn_from_bool(bool aValue) { return make_bool(aValue); }
        object* fn_from_int(std::int64_t aValue) { return make_int(aValue); }
        object* fn_from_float(double aValue) { return make_float(aValue); }
        object* fn_from_str(char const* aText) { return make_str(aText); }
        object* fn_from_complex(double aReal, double aImag) { return make_complex(aReal, aImag); }
        bool fn_truth(object* aObject) { return truth(aObject) == 1; }
        char const* fn_str(object* aObject)
        {
            std::string text;
            if (!str_of(aObject, text))
                return "";
            return result(std::move(text));
        }
        // (an f-string's replacement field: the value converted (aConversion: 0, 'r', 's' or 'a') and formatted)
        char const* fn_format_field(object* aObject, std::int32_t aConversion, char const* aSpec)
        {
            std::string text;
            if (!convert_field(aObject, static_cast<char>(aConversion), aSpec, text))
                return "";
            return result(std::move(text));
        }

        char const* fn_fstring(object* aObject, std::int64_t aConversion, char const* aSpec)
        {
            return fn_format_field(aObject, static_cast<std::int32_t>(aConversion), aSpec);
        }

        char const* fn_repr(object* aObject)
        {
            std::string text;
            if (!repr_of(aObject, text))
                return "";
            return result(std::move(text));
        }
        // int(x) and int(x, base) (aBase: -1 if not given); null if it fails
        object* int_from(object* aObject, long long aBase)
        {
            auto const k = kind_of(aObject);
            if (aBase != -1 && k != NEOS_PY_STR)
                return error("TypeError", "int() can't convert non-string with explicit base");
            if (aBase != -1 && aBase != 0 && (aBase < 2 || aBase > 36))
                return error("ValueError", "int() base must be >= 2 and <= 36, or 0");
            switch (k)
            {
            case NEOS_PY_FLOAT:
                {
                    auto const v = as<float_object>(aObject)->value;
                    if (std::isnan(v))
                        return error("ValueError", "cannot convert float NaN to integer");
                    if (std::isinf(v))
                        return error("OverflowError", "cannot convert float infinity to integer");
                    if (v >= 9223372036854775808.0 || v < -9223372036854775808.0)
                        return make_int(big_from_double(v));
                    return make_int(static_cast<long long>(v));
                }
            case NEOS_PY_STR:
                {
                    // (as CPython: surrounding whitespace, a sign, a prefix that matches the base (any if it is 0), single
                    // underscores between digits)
                    auto const& text = as<str_object>(aObject)->value;
                    long long const givenBase = aBase == -1 ? 10 : aBase;
                    auto const invalid = [&]()
                    {
                        return error("ValueError", "invalid literal for int() with base " + std::to_string(givenBase) + ": " + str_repr(text));
                    };
                    auto const begin = text.find_first_not_of(" \t\n\r\f\v");
                    if (begin == std::string::npos)
                        return invalid();
                    auto const end = text.find_last_not_of(" \t\n\r\f\v") + 1u;
                    std::string_view body{ text.data() + begin, end - begin };
                    bool negative = false;
                    if (!body.empty() && (body[0] == '+' || body[0] == '-'))
                    {
                        negative = body[0] == '-';
                        body.remove_prefix(1u);
                    }
                    long long base = givenBase;
                    bool prefixed = false;
                    if (body.size() >= 2u && body[0] == '0')
                    {
                        auto const p = static_cast<char>(std::tolower(static_cast<unsigned char>(body[1])));
                        long long const prefixBase = p == 'x' ? 16 : p == 'o' ? 8 : p == 'b' ? 2 : 0;
                        if (prefixBase != 0 && (base == 0 || base == prefixBase))
                        {
                            base = prefixBase;
                            body.remove_prefix(2u);
                            prefixed = true;
                        }
                    }
                    if (base == 0)
                        base = 10;
                    std::string digits;
                    bool underscore = prefixed; // (an underscore may follow a prefix)
                    for (auto c : body)
                    {
                        if (c == '_')
                        {
                            if (underscore || (digits.empty() && !prefixed))
                                return invalid();
                            underscore = true;
                            continue;
                        }
                        int const d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'z' ? c - 'a' + 10 : c >= 'A' && c <= 'Z' ? c - 'A' + 10 : 99;
                        if (d >= base)
                            return invalid();
                        digits += c;
                        underscore = false;
                    }
                    if (digits.empty() || underscore)
                        return invalid();
                    // (base 0: a decimal number other than zero cannot have leading zeros)
                    if (aBase == 0 && !prefixed && digits.size() > 1u && digits[0] == '0' && digits.find_first_not_of('0') != std::string::npos)
                        return invalid();
                    if ((base & (base - 1)) != 0 && sIntMaxStrDigits > 0 && digits.size() > static_cast<std::size_t>(sIntMaxStrDigits))
                        return int_max_str_digits_error(digits.size(), true);
                    unsigned long long value = 0u;
                    auto const converted = std::from_chars(digits.data(), digits.data() + digits.size(), value, static_cast<int>(base));
                    if (converted.ec == std::errc{} && converted.ptr == digits.data() + digits.size() && value <= 9223372036854775807ull)
                        return make_int(negative ? -static_cast<long long>(value) : static_cast<long long>(value));
                    return digits_int(digits, static_cast<unsigned>(base), negative);
                }
            case NEOS_PY_BOOL:
                return make_int(int_value(aObject));
            case NEOS_PY_INT:
                retain(aObject);
                return aObject;
            case NEOS_PY_OTHER:
                if (auto const type = python_type(aObject); type != nullptr && (type->to_int != nullptr || type->index != nullptr))
                {
                    long long value = 0;
                    if ((type->to_int != nullptr ? type->to_int(context(), aObject, &value) : type->index(context(), aObject, &value)) != 0)
                        return nullptr;
                    return make_int(value);
                }
                // (its __int__ (or __index__): e.g. a UUID's, which can be larger than 64 bits)
                for (auto method : { "__int__", "__index__" })
                    if (ref const m{ getattr(aObject, method) })
                    {
                        ref const empty{ make_tuple() };
                        ref result{ call(m.get(), empty.get(), nullptr) };
                        if (result && kind_of(result.get()) != NEOS_PY_INT && kind_of(result.get()) != NEOS_PY_BOOL)
                            return error("TypeError", std::string{ method } + " returned non-int (type " + type_name(result.get()) + ")");
                        return result.release_ownership();
                    }
                    else
                        clear_error();
                [[fallthrough]];
            default:
                return error("TypeError", std::string{ "int() argument must be a string, a bytes-like object or a real number, not '" } + type_name(aObject) + "'");
            }
        }

        std::int64_t fn_to_int(object* aObject)
        {
            // int(x) (a native int: OverflowError beyond 64 bits)
            ref const value{ int_from(aObject, -1) };
            if (!value)
                return 0;
            return int_value(value.get());
        }

        double fn_to_float(object* aObject)
        {
            if (kind_of(aObject) == NEOS_PY_STR)
            {
                auto const& text = as<str_object>(aObject)->value;
                char* end = nullptr;
                auto const value = std::strtod(text.c_str(), &end);
                std::string_view rest{ end };
                if (end == text.c_str() || rest.find_first_not_of(" \t\n\r\f\v") != std::string_view::npos)
                    return (error("ValueError", "could not convert string to float: " + str_repr(text)), 0.0);
                return value;
            }
            double value = 0.0;
            if (!to_double(aObject, value))
            {
                python_runtime::raise("TypeError", std::string{ "float() argument must be a string or a real number, not '" } + type_name(aObject) + "'");
                return 0.0;
            }
            return value;
        }
        // (an int literal beyond 64 bits)
        object* fn_int_from_string(char const* aText) { return make_int(big_integer::from_string(std::string{ aText })); }
        // (two ints of 64 bits and an arithmetic operator: int_binary's, without binary's dispatch)
        bool small_arithmetic(std::int32_t aOperation, object* aLhs, object* aRhs)
        {
            return is_small_int(aLhs) && is_small_int(aRhs) && (aOperation == NEOS_PY_ADD || aOperation == NEOS_PY_SUBTRACT || 
                aOperation == NEOS_PY_MULTIPLY || aOperation == NEOS_PY_TRUE_DIVIDE || aOperation == NEOS_PY_FLOOR_DIVIDE || aOperation == NEOS_PY_REMAINDER);
        }
        // (and the commonest: a sum or difference that does not overflow)
        inline object* small_sum(std::int32_t aOperation, object* aLhs, object* aRhs)
        {
            if (!is_small_int(aLhs) || !is_small_int(aRhs))
                return nullptr;
            auto const a = as<int_object>(aLhs)->value;
            auto const b = as<int_object>(aRhs)->value;
            long long r = 0;
            if (aOperation == NEOS_PY_ADD ? !add_overflow(a, b, &r) : aOperation == NEOS_PY_SUBTRACT && !subtract_overflow(a, b, &r))
                return make_int(r);
            return nullptr;
        }
        object* fn_binary(std::int32_t aOperation, object* aLhs, object* aRhs) 
        { 
            if (auto const sum = small_sum(aOperation, aLhs, aRhs))
                return sum;
            if (small_arithmetic(aOperation, aLhs, aRhs))
                return checked(int_binary(aOperation, as<int_object>(aLhs)->value, as<int_object>(aRhs)->value));
            return checked(binary(aOperation, aLhs, aRhs, false)); 
        }
        object* fn_inplace(std::int32_t aOperation, object* aLhs, object* aRhs) 
        { 
            if (auto const sum = small_sum(aOperation, aLhs, aRhs))
                return sum;
            if (small_arithmetic(aOperation, aLhs, aRhs))
                return checked(int_binary(aOperation, as<int_object>(aLhs)->value, as<int_object>(aRhs)->value));
            return checked(binary(aOperation, aLhs, aRhs, true)); 
        }
        // (an int (NEOS_RT_INTEGER: e.g. an int loop's variable) and an object (python::binary_io: the int is the left operand; 
        // python::binary_oi: the right; aOperation: the operator, with 0x100 set if it is in place): small ints are not made objects)
        object* integer_as_object(std::int64_t aInteger)
        {
            if (integer_is_object(aInteger))
            {
                auto const result = integer_object(aInteger);
                retain(result);
                return result;
            }
            return make_int(aInteger >> 1);
        }
        object* small_binary(std::int32_t aOperation, long long a, long long b)
        {
            long long r = 0;
            if (aOperation == NEOS_PY_ADD && !add_overflow(a, b, &r))
                return make_int(r);
            if (aOperation == NEOS_PY_SUBTRACT && !subtract_overflow(a, b, &r))
                return make_int(r);
            if (aOperation == NEOS_PY_ADD || aOperation == NEOS_PY_SUBTRACT || aOperation == NEOS_PY_MULTIPLY || aOperation == NEOS_PY_TRUE_DIVIDE || 
                aOperation == NEOS_PY_FLOOR_DIVIDE || aOperation == NEOS_PY_REMAINDER)
                return checked(int_binary(aOperation, a, b));
            return nullptr;
        }
        object* fn_binary_io(std::int32_t aOperation, std::int64_t aInteger, object* aObject)
        {
            auto const op = aOperation & 0xFF;
            if (!integer_is_object(aInteger) && is_small_int(aObject))
                if (auto const result = small_binary(op, aInteger >> 1, as<int_object>(aObject)->value); result != nullptr || failed())
                    return result;
            ref const lhs{ integer_as_object(aInteger) };
            return checked(binary(op, lhs.get(), aObject, (aOperation & 0x100) != 0));
        }
        object* fn_binary_oi(std::int32_t aOperation, object* aObject, std::int64_t aInteger)
        {
            auto const op = aOperation & 0xFF;
            if (!integer_is_object(aInteger) && is_small_int(aObject))
                if (auto const result = small_binary(op, as<int_object>(aObject)->value, aInteger >> 1); result != nullptr || failed())
                    return result;
            ref const rhs{ integer_as_object(aInteger) };
            return checked(binary(op, aObject, rhs.get(), (aOperation & 0x100) != 0));
        }
        // (and comparisons: their truth (python::compare_io_truth and compare_oi_truth: no object for the result), or the result)
        bool small_compare(std::int32_t aOperation, long long a, long long b)
        {
            switch (aOperation)
            {
            case NEOS_PY_LT: return a < b;
            case NEOS_PY_LE: return a <= b;
            case NEOS_PY_EQ: return a == b;
            case NEOS_PY_NE: return a != b;
            case NEOS_PY_GT: return a > b;
            default: return a >= b;
            }
        }
        // (ints (or bools) that are not both small compared by their order, with no bool made (2: they are not))
        int int_compare(std::int32_t aOperation, object* aLhs, object* aRhs)
        {
            auto const order = limbs_order(aLhs, aRhs);
            return order == 2 ? 2 : small_compare(aOperation, order, 0) ? 1 : 0;
        }
        object* fn_compare_io(std::int32_t aOperation, std::int64_t aInteger, object* aObject)
        {
            if (!integer_is_object(aInteger) && is_small_int(aObject))
                return make_bool(small_compare(aOperation, aInteger >> 1, as<int_object>(aObject)->value));
            ref const lhs{ integer_as_object(aInteger) };
            return checked(compare(aOperation, lhs.get(), aObject));
        }
        object* fn_compare_oi(std::int32_t aOperation, object* aObject, std::int64_t aInteger)
        {
            if (!integer_is_object(aInteger) && is_small_int(aObject))
                return make_bool(small_compare(aOperation, as<int_object>(aObject)->value, aInteger >> 1));
            ref const rhs{ integer_as_object(aInteger) };
            return checked(compare(aOperation, aObject, rhs.get()));
        }
        bool fn_compare_io_truth(std::int32_t aOperation, std::int64_t aInteger, object* aObject)
        {
            if (!integer_is_object(aInteger) && is_small_int(aObject))
                return small_compare(aOperation, aInteger >> 1, as<int_object>(aObject)->value);
            ref const lhs{ integer_as_object(aInteger) };
            if (auto const r = int_compare(aOperation, lhs.get(), aObject); r != 2)
                return r == 1;
            ref const result{ compare(aOperation, lhs.get(), aObject) };
            if (result.get() == nullptr)
                return (checked(nullptr), false);
            return truth(result.get()) == 1;
        }
        bool fn_compare_oi_truth(std::int32_t aOperation, object* aObject, std::int64_t aInteger)
        {
            if (!integer_is_object(aInteger) && is_small_int(aObject))
                return small_compare(aOperation, as<int_object>(aObject)->value, aInteger >> 1);
            ref const rhs{ integer_as_object(aInteger) };
            if (auto const r = int_compare(aOperation, aObject, rhs.get()); r != 2)
                return r == 1;
            ref const result{ compare(aOperation, aObject, rhs.get()) };
            if (result.get() == nullptr)
                return (checked(nullptr), false);
            return truth(result.get()) == 1;
        }
        object* fn_unary(std::int32_t aOperation, object* aObject) { return checked(unary(aOperation, aObject)); }
        object* fn_compare(std::int32_t aOperation, object* aLhs, object* aRhs) { return checked(compare(aOperation, aLhs, aRhs)); }
        // (a comparison's truth (e.g. if a == b:): its result is not kept)
        bool fn_compare_truth(std::int32_t aOperation, object* aLhs, object* aRhs)
        {
            if (auto const r = int_compare(aOperation, aLhs, aRhs); r != 2)
                return r == 1;
            ref const result{ compare(aOperation, aLhs, aRhs) };
            if (result.get() == nullptr)
                return (checked(nullptr), false);
            return truth(result.get()) == 1;
        }
        object* compare_made_text(std::int32_t aOperation, object* aObject, char const* aText);
        // (a str that the program has as text (e.g. a variable bound to a literal) compared (== or !=) with an object (aOperation: the 
        // operator, with 0x100 set if the text is the left operand): as text if the object is a str (no str is made for the text), 
        // else as compare's)
        inline object* compare_text(std::int32_t aOperation, object* aObject, char const* aText, int& aResult)
        {
            auto const op = aOperation & 0xFF;
            if (kind_of(aObject) == NEOS_PY_STR && (op == NEOS_PY_EQ || op == NEOS_PY_NE))
            {
                auto const& value = as<str_object>(aObject)->value;
                bool const equal = value.compare(aText) == 0;
                aResult = equal == (op == NEOS_PY_EQ) ? 1 : 0;
                return nullptr;
            }
            return compare_made_text(aOperation, aObject, aText);
        }
        object* compare_made_text(std::int32_t aOperation, object* aObject, char const* aText)
        {
            auto const op = aOperation & 0xFF;
            ref const text{ make_str(aText) };
            return (aOperation & 0x100) != 0 ? compare(op, text.get(), aObject) : compare(op, aObject, text.get());
        }
        object* fn_compare_text(std::int32_t aOperation, object* aObject, char const* aText)
        {
            int result = -1;
            auto const r = compare_text(aOperation, aObject, aText, result);
            return result >= 0 ? make_bool(result == 1) : checked(r);
        }
        bool fn_compare_text_truth(std::int32_t aOperation, object* aObject, char const* aText)
        {
            int result = -1;
            ref const r{ compare_text(aOperation, aObject, aText, result) };
            if (result >= 0)
                return result == 1;
            if (r.get() == nullptr)
                return (checked(nullptr), false);
            return truth(r.get()) == 1;
        }
        bool fn_is(object* aLhs, object* aRhs) { return aLhs == aRhs; }

        // --- integers of any size (NEOS_RT_INTEGER: a small integer shifted left one bit, or an int object's address with the low
        // bit set); an argument is borrowed, a result is a new reference

        // (an integer as an int object: a new reference)
        object* integer_to_object(std::int64_t aValue)
        {
            if (integer_is_object(aValue))
            {
                auto const result = integer_object(aValue);
                retain(result);
                return result;
            }
            return make_int(aValue >> 1);
        }

        // (an int (a new reference) as an integer: a small integer if it is one; 0 if there is no result (an exception))
        std::int64_t integer_result(object* aResult)
        {
            if (aResult == nullptr)
                return (checked(nullptr), 0);
            // (an int (not a subclass's): with no dispatch)
            if (aResult->type == &sIntType.base)
            {
                if (is_small_int(aResult))
                {
                    auto const value = as<int_object>(aResult)->value;
                    if (value >= -sSmallIntegerLimit && value < sSmallIntegerLimit)
                    {
                        release(aResult);
                        return small_integer(value);
                    }
                }
                return static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(aResult) | 1u);
            }
            if (kind_of(aResult) == NEOS_PY_BOOL)
            {
                bool const value = aResult == &sTrue.base;
                release(aResult);
                return small_integer(value ? 1 : 0);
            }
            if (kind_of(aResult) != NEOS_PY_INT)
            {
                ref const discarded{ aResult };
                return (error("TypeError", std::string{ "an integer is required, not '" } + type_name(aResult) + "'"), 0);
            }
            if (!is_big(aResult))
            {
                auto const value = as<int_object>(aResult)->value;
                if (value >= -sSmallIntegerLimit && value < sSmallIntegerLimit)
                {
                    release(aResult);
                    return small_integer(value);
                }
            }
            return static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(aResult) | 1u);
        }

        // (an integer as an int object: borrowed (an integer object) or owned by aOwned)
        object* integer_borrowed(std::int64_t aValue, ref& aOwned)
        {
            if (integer_is_object(aValue))
                return integer_object(aValue);
            aOwned = ref{ make_int(aValue >> 1) };
            return aOwned.get();
        }

        // (an integer's limbs (a small integer's: no object is made for it))
        bool integer_limbs(std::int64_t aValue, limb_view& aView)
        {
            if (integer_is_object(aValue))
                return limbs_of(integer_object(aValue), aView);
            view_of(int128_of(aValue >> 1), aView);
            return true;
        }

        std::int64_t fn_integer_binary(std::int32_t aOperation, std::int64_t aLhs, std::int64_t aRhs)
        {
            if (!integer_is_object(aLhs) && !integer_is_object(aRhs))
                return integer_result(int_binary(aOperation, aLhs >> 1, aRhs >> 1));
            // (+, - and * of an int beyond 128 bits: of their limbs, with no dispatch (x + 0, x - 0 and 0 + x: x))
            if (limb_view a, b; (aOperation == NEOS_PY_ADD || aOperation == NEOS_PY_SUBTRACT || aOperation == NEOS_PY_MULTIPLY) &&
                integer_limbs(aLhs, a) && integer_limbs(aRhs, b) && (a.size > 2u || b.size > 2u))
            {
                if (aOperation != NEOS_PY_MULTIPLY && b.size == 0u && integer_object(aLhs)->type == &sIntType.base)
                    return (retain(integer_object(aLhs)), aLhs);
                if (aOperation == NEOS_PY_ADD && a.size == 0u && integer_object(aRhs)->type == &sIntType.base)
                    return (retain(integer_object(aRhs)), aRhs);
                return integer_result(aOperation == NEOS_PY_MULTIPLY ? multiply_views(a, b) : add_views(a, b, aOperation == NEOS_PY_SUBTRACT));
            }
            ref l, r;
            return integer_result(binary(aOperation, integer_borrowed(aLhs, l), integer_borrowed(aRhs, r), false));
        }

        std::int64_t fn_integer_unary(std::int32_t aOperation, std::int64_t aValue)
        {
            ref v;
            return integer_result(unary(aOperation, integer_borrowed(aValue, v)));
        }

        bool fn_integer_compare(std::int32_t aOperation, std::int64_t aLhs, std::int64_t aRhs)
        {
            ref l, r;
            ref const result{ compare(aOperation, integer_borrowed(aLhs, l), integer_borrowed(aRhs, r)) };
            if (!result)
                return (checked(nullptr), false);
            return truth(result.get()) == 1;
        }

        double fn_integer_true_divide(std::int64_t aLhs, std::int64_t aRhs)
        {
            ref l, r;
            ref const result{ binary(NEOS_PY_TRUE_DIVIDE, integer_borrowed(aLhs, l), integer_borrowed(aRhs, r), false) };
            if (!result)
                return (checked(nullptr), 0.0);
            return as<float_object>(result.get())->value;
        }

        double fn_integer_float(std::int64_t aValue)
        {
            if (!integer_is_object(aValue))
                return static_cast<double>(aValue >> 1);
            double result = 0.0;
            if (!to_double(integer_object(aValue), result))
                return 0.0;
            return result;
        }

        std::int64_t fn_integer_i64(std::int64_t aValue)
        {
            if (!integer_is_object(aValue))
                return aValue >> 1;
            return int_value(integer_object(aValue));
        }

        char const* fn_integer_str(std::int64_t aValue)
        {
            if (!integer_is_object(aValue))
                return result(std::to_string(aValue >> 1));
            return fn_str(integer_object(aValue));
        }

        bool write_out(std::string const& aText);

        void fn_print_integer(std::int64_t aValue)
        {
            write_out(std::string{ fn_integer_str(aValue) } + "\n");
        }

        std::int64_t fn_integer_from_i64(std::int64_t aValue) { return integer_result(make_int(aValue)); }

        std::int64_t fn_integer_from_float(double aValue)
        {
            ref const value{ make_float(aValue) };
            return integer_result(int_from(value.get(), -1));
        }

        std::int64_t fn_integer_from_string(char const* aText) { return integer_result(make_int(big_integer::from_string(std::string{ aText }))); }
        std::int64_t fn_to_integer(object* aObject) { return integer_result(int_from(aObject, -1)); }
        object* fn_from_integer(std::int64_t aValue) { return integer_to_object(aValue); }

        // (builtins.py's stubs whose argument or result is an int)
        std::int64_t fn_int_string(char const* aText)
        {
            ref const value{ make_str(aText) };
            return integer_result(int_from(value.get(), -1));
        }
        std::int64_t fn_abs_integer(std::int64_t aValue) { return fn_integer_unary(NEOS_PY_ABSOLUTE, aValue); }
        std::int64_t fn_len_integer(object* aObject)
        {
            auto const result = length(aObject);
            return failed() ? 0 : small_integer(result);
        }
        char const* fn_fstring_integer(object* aObject, std::int64_t aConversion, char const* aSpec)
        {
            return fn_format_field(aObject, static_cast<std::int32_t>(aConversion >> 1), aSpec);
        }
        bool fn_contains(object* aContainer, object* aItem) { return contains(aContainer, aItem) == 1; }
        void note_attribute_error(object* aObject);
        object* fn_getattr(object* aObject, char const* aName) 
        { 
            auto const result = getattr(aObject, aName);
            if (result == nullptr)
                note_attribute_error(aObject);
            return checked(result); 
        }
        void fn_setattr(object* aObject, char const* aName, object* aValue) { setattr(aObject, aName, aValue); }
        object* fn_call(object* aCallable, object* aArgs, object* aKwargs) { gil_check(); return signals_check() ? checked(call(aCallable, aArgs, aKwargs)) : nullptr; }
        object* load_method(object* aObject, char const* aName);
        object* arguments_call(object* aCallable, std::span<object* const> aItems, object* aArgs, object* aKwargs);
        object* fn_load_method(object* aObject, char const* aName) { return checked(load_method(aObject, aName)); }
        // (a call of up to three positional arguments (those that are not null): with no tuple made for them if the callable is 
        // load_method's method, a function of the program or a method that is one (four parameters: native code calls it directly))
        object* fn_call_arguments(object* aCallable, object* a0, object* a1, object* a2)
        {
            gil_check();
            object* const items[] = { a0, a1, a2 };
            auto const count = a0 == nullptr ? 0u : a1 == nullptr ? 1u : a2 == nullptr ? 2u : 3u;
            return signals_check() ? checked(arguments_call(aCallable, std::span<object* const>{ items, count }, nullptr, nullptr)) : nullptr;
        }
        object* fn_getitem(object* aObject, object* aKey) { return checked(getitem(aObject, aKey)); }
        // (an item whose key is an integer (NEOS_RT_INTEGER): a str's, list's or tuple's item at a small integer without an int 
        // object; otherwise getitem's)
        object* fn_getitem_integer(object* aObject, std::int64_t aKey)
        {
            auto const k = kind_of(aObject);
            if (!integer_is_object(aKey) && (k == NEOS_PY_LIST || k == NEOS_PY_TUPLE || k == NEOS_PY_STR))
            {
                long long i = aKey >> 1;
                auto const size = k == NEOS_PY_STR ? as<str_object>(aObject)->len() : static_cast<long long>(k == NEOS_PY_LIST ? 
                    as<list_object>(aObject)->items.size() : as<tuple_object>(aObject)->items.size());
                if (i < 0)
                    i += size;
                if (i < 0 || i >= size)
                {
                    error("IndexError", k == NEOS_PY_LIST ? "list index out of range" : k == NEOS_PY_TUPLE ? "tuple index out of range" : "string index out of range");
                    return nullptr;
                }
                if (k != NEOS_PY_STR)
                {
                    auto* const item = (k == NEOS_PY_LIST ? as<list_object>(aObject)->items : as<tuple_object>(aObject)->items)[static_cast<std::size_t>(i)];
                    retain(item);
                    return item;
                }
                auto const& s = *as<str_object>(aObject);
                if (s.is_ascii())
                    return ascii_character(s.ascii_at(static_cast<std::size_t>(i)));
                auto const character = s.at(i);
                if (character < 0x80u)
                    return ascii_character(static_cast<char>(character));
                return make_str(neos::runtime::utf8_of(character), 1u);
            }
            ref const key{ integer_to_object(aKey) };
            return checked(getitem(aObject, key.get()));
        }
        void fn_setitem(object* aObject, object* aKey, object* aValue) { setitem(aObject, aKey, aValue); }
        void fn_delitem(object* aObject, object* aKey) { setitem(aObject, aKey, nullptr); }
        void fn_delattr(object* aObject, char const* aName) { setattr(aObject, aName, nullptr); }
        std::int64_t fn_len(object* aObject) { return length(aObject); }
        object* fn_iter(object* aObject) { return checked(iter(aObject)); }
        object* fn_next(object* aIterator) { gil_check(); return signals_check() ? next(aIterator) : nullptr; }
        // (a for loop over a range (see the Python compiler's py_for_in): a range whose start and stop are small integers (see 
        // NEOS_RT_INTEGER), so that its values are too: its length, else -1; its start and step)
        long long fn_range_length(object* aObject)
        {
            if (kind_of(aObject) != NEOS_PY_RANGE)
                return -1;
            constexpr long long SmallLow = -(1LL << 62);
            constexpr long long SmallHigh = (1LL << 62) - 1;
            auto const& r = *as<range_object>(aObject);
            if (r.start < SmallLow || r.start > SmallHigh || r.stop < SmallLow || r.stop > SmallHigh)
                return -1;
            // (unsigned: no overflow)
            if (r.step > 0)
                return r.start < r.stop ? static_cast<long long>((static_cast<unsigned long long>(r.stop - r.start) - 1u) / static_cast<unsigned long long>(r.step) + 1u) : 0;
            return r.start > r.stop ? static_cast<long long>((static_cast<unsigned long long>(r.start - r.stop) - 1u) / (0u - static_cast<unsigned long long>(r.step)) + 1u) : 0;
        }
        // (a list's or tuple's length (else -1), and its item at an index (a new reference; null if it has no such item (a list can 
        // become shorter while it is iterated over)): the loops that compiled code makes over them)
        long long fn_sequence_length(object* aObject)
        {
            auto const k = kind_of(aObject);
            if (k == NEOS_PY_LIST)
                return static_cast<long long>(as<list_object>(aObject)->items.size());
            if (k == NEOS_PY_TUPLE)
                return static_cast<long long>(as<tuple_object>(aObject)->items.size());
            return -1;
        }
        object* fn_sequence_item(object* aObject, std::int64_t aIndex)
        {
            auto const& items = kind_of(aObject) == NEOS_PY_LIST ? as<list_object>(aObject)->items : as<tuple_object>(aObject)->items;
            if (aIndex < 0 || static_cast<std::size_t>(aIndex) >= items.size())
                return nullptr;
            auto const item = items[static_cast<std::size_t>(aIndex)];
            retain(item);
            return item;
        }
        // (an int loop's item (see the compiler's py_for_in): a small int (shifted left one bit, as NEOS_RT_INTEGER's), or 1 (none: the 
        // end) or 3 (an item that is not a small int, e.g. a bool))
        std::int64_t fn_sequence_int(object* aObject, std::int64_t aIndex)
        {
            auto const& items = kind_of(aObject) == NEOS_PY_LIST ? as<list_object>(aObject)->items : as<tuple_object>(aObject)->items;
            if (aIndex < 0 || static_cast<std::size_t>(aIndex) >= items.size())
                return 1;
            auto const item = items[static_cast<std::size_t>(aIndex)];
            if (!is_small_int(item))
                return 3;
            auto const value = as<int_object>(item)->value;
            if (value < -sSmallIntegerLimit || value >= sSmallIntegerLimit)
                return 3;
            return static_cast<std::int64_t>(static_cast<std::uint64_t>(value) << 1u);
        }
        // (whether an int loop can bind a variable to its int (see the compiler's int loops): an int (not a bool))
        bool fn_is_int(object* aObject) { return aObject != nullptr && aObject->type == &sIntType.base; }
        long long fn_range_start(object* aObject) { return as<range_object>(aObject)->start; }
        long long fn_range_step(object* aObject) { return as<range_object>(aObject)->step; }
        object* fn_tuple(std::int64_t aSize) { return ctx_tuple_new(context(), static_cast<std::size_t>(aSize)); }
        void fn_tuple_set(object* aTuple, std::int64_t aIndex, object* aItem) { retain(aItem); ctx_tuple_set(context(), aTuple, static_cast<std::size_t>(aIndex), aItem); }
        object* fn_list() { return make_list(); }
        void fn_list_append(object* aList, object* aItem) { ctx_list_append(context(), aList, aItem); }
        object* fn_dict() { return make_dict(); }
        void fn_dict_set(object* aDict, object* aKey, object* aValue) { dict_set(aDict, aKey, aValue); }
        void fn_kwarg(object* aDict, char const* aName, object* aValue) { ref const key{ make_str(aName) }; dict_set(aDict, key.get(), aValue); }
        object* fn_slice(object* aStart, object* aStop, object* aStep) { return make_slice(aStart, aStop, aStep); }
        object* fn_range(std::int64_t aStart, std::int64_t aStop, std::int64_t aStep)
        {
            if (aStep == 0)
                return error("ValueError", "range() arg 3 must not be zero");
            return make_range(aStart, aStop, aStep);
        }
        object* fn_list_of(object* aIterable)
        {
            std::vector<object*> items;
            if (!collect(aIterable, items))
                return nullptr;
            return make_list(std::move(items));
        }
        object* fn_tuple_of(object* aIterable)
        {
            std::vector<object*> items;
            if (!collect(aIterable, items))
                return nullptr;
            return make_tuple(std::move(items));
        }

        // --- type objects (builtin classes: int, float, ...; the classes of other plugins' objects)

        struct type_object
        {
            header h;
            std::string name;
            std::string qualifiedName;
            native_function call = nullptr;     ///< (a class of another plugin: constructs its objects)
            void* data = nullptr;
            int (*instancecheck)(neos_py_context*, void*, object*) = nullptr;
        };
        object* call_builtin(neos_py_context*, void* aData, neos_py_object* aArgs, neos_py_object* aKwargs);
        neos_py_type make_type_type();
        neos_py_type sTypeType = make_type_type();

        object* make_type_object(std::string aName, std::string aQualifiedName)
        {
            return &(new type_object{ { { &sTypeType.base } }, std::move(aName), std::move(aQualifiedName) })->h.base;
        }

        // (the type object of a type (by its qualified name): one for each type, so that "type(1) is int"; a new reference)
        object* type_object_for(std::string const& aName, std::string const& aQualifiedName)
        {
            static std::map<std::string, object*, std::less<>> sTypes;
            auto& t = sTypes[aQualifiedName];
            if (t == nullptr)
                t = make_type_object(aName, aQualifiedName);
            retain(t);
            return t;
        }

        bool is_type_object(object* aObject)
        {
            return aObject != nullptr && aObject->type == &sTypeType.base;
        }

        std::string const& type_qualified_name(object* aType)
        {
            return as<type_object>(aType)->qualifiedName;
        }

        // the name of an object's class as Python shows it in "<class '...'>"
        std::string class_name(object* aObject)
        {
            if (is_type_object(aObject))
                return "type";
            if (kind_of(aObject) == NEOS_PY_OTHER)
                return aObject->type->name;
            return type_name(aObject);
        }

        builtin_entry const* find_builtin(std::string_view aName);
        object* class_function(std::string const& aClass, char const* aName);

        std::vector<std::string> exception_base_names(std::string_view aName);
        std::vector<std::string> exception_mro(std::string_view aName);
        std::vector<std::string> exception_subclass_names(std::string_view aName);

        object* builtin_init(neos_py_context*, void*, neos_py_object* aArgs, neos_py_object* aKwargs);
        object* new_instance(object* aClass, object* aArgs, object* aKwargs);
        bool is_class(object* aObject);

        // (a type of a module's __new__(cls, ...): an object of it, or an instance of a class that derives from it (cls))
        object* native_new(neos_py_context*, void*, neos_py_object* aArgs, neos_py_object* aKwargs)
        {
            auto const& args = tuple_items(aArgs);
            if (args.size() < 2u)
                return error("TypeError", "__new__(): not enough arguments");
            auto const type = args[0];
            auto const cls = args[1];
            ref const rest{ make_tuple(std::vector<object*>{ args.begin() + 2, args.end() }) };
            for (auto a : tuple_items(rest.get()))
                retain(a);
            if (is_class(cls))
                return new_instance(cls, rest.get(), aKwargs);
            if (cls != type)
                return error("TypeError", std::string{ as<type_object>(type)->name } + ".__new__(X): X is not a subtype of " + as<type_object>(type)->name);
            return call(type, rest.get(), aKwargs);
        }

        bool unhashable_builtin_type(std::string_view aQualifiedName)
        {
            static std::set<std::string, std::less<>> const sUnhashable = { "list", "dict", "set", "bytearray", "collections.deque", "collections.OrderedDict",
                "collections.defaultdict", "collections.Counter", "dict_keys", "dict_items" };
            return sUnhashable.contains(aQualifiedName);
        }

        // (a builtin type's __dict__ (a mappingproxy): its attributes that object does not have (and object's, for object), and its __hash__ (None 
        // if its instances are not hashable))
        object* builtin_type_dict(object* aType)
        {
            static std::map<object*, object*> sDicts;
            if (auto const existing = sDicts.find(aType); existing != sDicts.end())
                return make_mapping_proxy(existing->second);
            auto const& q = as<type_object>(aType)->qualifiedName;
            ref const objectType{ type_object_for("object", "object") };
            ref const names{ b_dir({ aType }, nullptr) };
            ref const objectNames{ q == "object" ? nullptr : b_dir({ objectType.get() }, nullptr) };
            if (!names || (q != "object" && !objectNames))
                return nullptr;
            ref result{ make_dict() };
            for (auto n : as<list_object>(names.get())->items)
            {
                if (kind_of(n) != NEOS_PY_STR)
                    continue;
                auto const& name = as<str_object>(n)->value;
                if (name == "__dict__")
                    continue;   // (the dict being made)
                bool inherited = false;
                if (objectNames)
                    for (auto o : as<list_object>(objectNames.get())->items)
                        if (kind_of(o) == NEOS_PY_STR && as<str_object>(o)->value == name)
                            inherited = true;
                if (inherited && !(name == "__hash__" && (unhashable_builtin_type(q) || q == "int" || q == "str" || q == "float" || q == "tuple" || 
                    q == "bytes" || q == "frozenset" || q == "complex" || q == "range")))
                    continue;
                ref const value{ getattr(aType, name.c_str()) };
                if (!value)
                {
                    clear_error();
                    continue;
                }
                if (dict_set(result.get(), n, value.get()) < 0)
                    return nullptr;
            }
            sDicts[aType] = new_reference(result.get());
            retain(aType);
            return make_mapping_proxy(result.get());
        }

        neos_py_type make_type_type()
        {
            auto type = make_type("python.type", "type", retain_object, release_object<type_object>);
            type.repr = [](neos_py_context*, neos_py_object* aSelf) -> neos_py_object*
            {
                return make_str("<class '" + as<type_object>(aSelf)->qualifiedName + "'>");
            };
            type.call = [](neos_py_context* aContext, neos_py_object* aSelf, neos_py_object* aArgs, neos_py_object* aKwargs) -> neos_py_object*
            {
                if (as<type_object>(aSelf)->call != nullptr)
                    return as<type_object>(aSelf)->call(aContext, as<type_object>(aSelf)->data, aArgs, aKwargs);
                auto const* builtin = find_builtin(as<type_object>(aSelf)->name);
                if (builtin == nullptr || as<type_object>(aSelf)->name != as<type_object>(aSelf)->qualifiedName)
                    return error("TypeError", "cannot create '" + as<type_object>(aSelf)->qualifiedName + "' instances");
                return call_builtin(aContext, const_cast<builtin_entry*>(builtin), aArgs, aKwargs);
            };
            type.compare = [](neos_py_context*, int aOperation, neos_py_object* aLhs, neos_py_object* aRhs) -> neos_py_object*
            {
                if ((aOperation != NEOS_PY_EQ && aOperation != NEOS_PY_NE) || !is_type_object(aLhs) || !is_type_object(aRhs))
                    return not_implemented();
                bool const equal = as<type_object>(aLhs)->qualifiedName == as<type_object>(aRhs)->qualifiedName;
                return make_bool(aOperation == NEOS_PY_EQ ? equal : !equal);
            };
            type.getattr = [](neos_py_context*, neos_py_object* aSelf, char const* aName) -> neos_py_object*
            {
                if (std::strcmp(aName, "__name__") == 0)
                    return make_str(as<type_object>(aSelf)->name);
                if (std::strcmp(aName, "__qualname__") == 0)
                    return make_str(as<type_object>(aSelf)->name);
                // (a builtin exception class's bases and MRO)
                if (auto const& q = as<type_object>(aSelf)->qualifiedName; is_exception_class(q) &&
                    (std::strcmp(aName, "__mro__") == 0 || std::strcmp(aName, "__bases__") == 0 || std::strcmp(aName, "__base__") == 0))
                {
                    std::vector<object*> classes;
                    if (aName[2] == 'm')
                        for (auto const& n : exception_mro(q))
                            classes.push_back(n == "object" ? type_object_for("object", "object") : exception_class(n));
                    else
                    {
                        for (auto const& n : exception_base_names(q))
                            classes.push_back(exception_class(n));
                        if (classes.empty())
                            classes.push_back(type_object_for("object", "object"));
                        if (aName[6] == '_')
                        {
                            auto const first = classes[0];
                            for (std::size_t i = 1u; i < classes.size(); ++i)
                                release(classes[i]);
                            return first;
                        }
                    }
                    return make_tuple(std::move(classes));
                }
                if (is_exception_class(as<type_object>(aSelf)->qualifiedName) && std::strcmp(aName, "__subclasses__") == 0)
                    return make_function("__subclasses__", [](neos_py_context*, void* aData, object*, object*) -> object*
                    {
                        std::vector<object*> result;
                        for (auto const& n : exception_subclass_names(*static_cast<std::string const*>(aData)))
                            result.push_back(exception_class(n));
                        return make_list(std::move(result));
                    }, const_cast<std::string*>(&as<type_object>(aSelf)->qualifiedName));
                // (the builtin types whose instances are not hashable: their __hash__ is None)
                if (std::strcmp(aName, "__hash__") == 0 && unhashable_builtin_type(as<type_object>(aSelf)->qualifiedName))
                    return new_reference(none());
                if (auto const function = class_function(as<type_object>(aSelf)->qualifiedName, aName))
                    return function;
                // (a type of a module: its __new__ (of it, or of a class that derives from it) and __init__ (of an instance of such a class))
                if (as<type_object>(aSelf)->call != nullptr && (std::strcmp(aName, "__new__") == 0 || std::strcmp(aName, "__init__") == 0))
                    return make_function(aName, std::strcmp(aName, "__new__") == 0 ? native_new : builtin_init, nullptr, std::strcmp(aName, "__new__") == 0 ? aSelf : nullptr);
                // (a builtin type's bases and MRO (e.g. bool's: int, object))
                if (std::strcmp(aName, "__mro__") == 0 || std::strcmp(aName, "__bases__") == 0 || std::strcmp(aName, "__base__") == 0)
                {
                    static std::map<std::string, std::string, std::less<>> const sBases = { { "bool", "int" }, { "collections.OrderedDict", "dict" },
                        { "collections.defaultdict", "dict" }, { "collections.Counter", "dict" } };
                    auto const& q = as<type_object>(aSelf)->qualifiedName;
                    if (q == "object")
                        return aName[2] == 'm' ? make_tuple({ new_reference(aSelf) }) : aName[6] == '_' ? new_reference(none()) : make_tuple();
                    std::vector<object*> mro{ new_reference(aSelf) };
                    for (auto b = sBases.find(q); b != sBases.end(); b = sBases.find(b->second))
                        mro.push_back(type_object_for(b->second.substr(b->second.rfind('.') + 1u), b->second));
                    mro.push_back(type_object_for("object", "object"));
                    if (aName[2] == 'm')
                        return make_tuple(std::move(mro));
                    auto const base = mro[1];
                    for (auto m : mro)
                        if (m != base)
                            release(m);
                    return aName[6] == '_' ? base : make_tuple({ base });
                }
                if (std::strcmp(aName, "__doc__") == 0)
                    return new_reference(none());
                if (std::strcmp(aName, "__dict__") == 0)
                    return builtin_type_dict(aSelf);
                if (std::strcmp(aName, "__subclasses__") == 0)
                    return make_function("__subclasses__", [](neos_py_context*, void*, object*, object*) -> object* { return make_list(); }, nullptr);
                if (std::strcmp(aName, "__module__") == 0)
                {
                    auto const& q = as<type_object>(aSelf)->qualifiedName;
                    auto const dot = q.rfind('.');
                    return make_str(dot == std::string::npos ? std::string{ "builtins" } : q.substr(0u, dot));
                }
                // (an attribute it inherits from object (e.g. NoneType.__hash__))
                if (as<type_object>(aSelf)->qualifiedName != "object")
                    if (auto const function = class_function("object", aName))
                        return function;
                return error("AttributeError", "type object '" + as<type_object>(aSelf)->name + "' has no attribute '" + aName + "'");
            };
            type.getitem = [](neos_py_context*, neos_py_object* aSelf, neos_py_object* aKey) -> neos_py_object*
            {
                // (list[int] etc.: a types.GenericAlias)
                static std::set<std::string, std::less<>> const sGeneric = { "list", "dict", "tuple", "set", "frozenset", "type", "collections.deque",
                    "collections.defaultdict", "collections.OrderedDict", "collections.Counter", "collections.ChainMap", "re.Pattern", "re.Match",
                    "queue.Queue", "queue.LifoQueue", "queue.PriorityQueue", "_queue.SimpleQueue", "enumerate", "zip", "map", "filter", "reversed" };
                auto const& name = as<type_object>(aSelf)->qualifiedName;
                if (sGeneric.contains(name) || as<type_object>(aSelf)->instancecheck != nullptr)
                    return make_generic_alias(aSelf, aKey);
                return error("TypeError", "type '" + as<type_object>(aSelf)->name + "' is not subscriptable");
            };
            return type;
        }

        object* ctx_type_new(neos_py_context*, char const* aName, char const* aQualifiedName, native_function aCall, void* aData, 
            int (*aInstancecheck)(neos_py_context*, void*, object*))
        {
            auto const result = type_object_for(aName, aQualifiedName);
            as<type_object>(result)->call = aCall;
            as<type_object>(result)->data = aData;
            as<type_object>(result)->instancecheck = aInstancecheck;
            return result;
        }

        // type(x)
        object* exception_class(std::string const& aName);

        object* class_metaclass(object* aObject);   // (a class's metaclass (borrowed): null if none (type))
        object* fn_type_new(neos_py_context*, void*, object* aArgs, object* aKwargs);

        char const* wrapper_type_name(object* aObject);   // (staticmethod, classmethod, property or cached_property: null if not one)

        object* class_of(object* aObject)
        {
            if (auto const wrapper = wrapper_type_name(aObject))
                return type_object_for(wrapper, wrapper);
            if (auto const cls = user_class_of(aObject))
            {
                retain(cls);
                return cls;
            }
            if (auto const metaclass = class_metaclass(aObject))
                return (retain(metaclass), metaclass);
            if (is_type_object(aObject) || is_class(aObject))
                return type_object_for("type", "type");
            if (auto const name = exception_type_name(aObject))
                return exception_class(name);
            if (kind_of(aObject) == NEOS_PY_OTHER)
            {
                std::string qualified = aObject->type->name;
                // (this plugin's own types (e.g. python.function) are builtins)
                if (qualified.starts_with("python."))
                    qualified.erase(0u, 7u);
                auto const type = python_type(aObject);
                std::string const name = type != nullptr ? type->name : qualified;
                if (type != nullptr)
                    observed_types().try_emplace(qualified, type);
                return type_object_for(name, qualified);
            }
            std::string const name = kind_of(aObject) == NEOS_PY_FUNCTION ? "builtin_function_or_method" : type_name(aObject);
            return type_object_for(name, name);
        }

        // --- exceptions (instances of BaseException's classes)

        // (the attributes of an instance (or of an exception): its __dict__ (made when it is first needed))
        // (a key of an attribute dict that is the name aName: its size and last byte (names often share a prefix, e.g. "_") are 
        // compared first)
        inline bool attribute_name(object* aKey, std::string_view aName)
        {
            if (kind_of(aKey) != NEOS_PY_STR)
                return false;
            auto const& key = as<str_object>(aKey)->value;
            return key.size() == aName.size() && (aName.empty() || (key.back() == aName.back() && std::memcmp(key.data(), aName.data(), aName.size()) == 0));
        }

        struct attribute_dict
        {
            object* dict = nullptr;
            attribute_dict() = default;
            attribute_dict(attribute_dict const&) = delete;
            ~attribute_dict() { release(dict); }
            object* find(std::string_view aName) const    // (borrowed: null if none)
            {
                if (dict == nullptr)
                    return nullptr;
                auto const& d = *as<dict_object>(dict);
                for (auto const& item : d.items)
                    if (attribute_name(item.first, aName))
                        return item.second;
                return nullptr;
            }
            object* get_dict()                              // (borrowed)
            {
                if (dict == nullptr)
                    dict = make_dict();
                return dict;
            }
            void set(std::string_view aName, object* aValue)
            {
                if (dict != nullptr)
                    for (auto& item : as<dict_object>(dict)->items)
                        if (attribute_name(item.first, aName))
                        {
                            retain(aValue);
                            release(item.second);
                            item.second = aValue;
                            return;
                        }
                ref const key{ make_str(std::string{ aName }) };
                dict_set(get_dict(), key.get(), aValue);
            }
            bool erase(std::string_view aName)
            {
                if (dict == nullptr)
                    return false;
                auto& d = *as<dict_object>(dict);
                for (std::size_t i = 0u; i < d.items.size(); ++i)
                    if (attribute_name(d.items[i].first, aName))
                    {
                        dict_erase(d, i);
                        return true;
                    }
                return false;
            }
        };

        struct exception_object
        {
            header h;
            std::string type;
            std::vector<object*> args;
            bool fromText = false;  ///< (raised by a plugin: its message is its str())
            object* cls = nullptr;  ///< (a user exception class)
            attribute_dict attributes;
            ~exception_object() { for (auto a : args) release(a); release(cls); }
        };


        // (the classes' bases)
        std::map<std::string, std::vector<std::string>, std::less<>> const& exception_bases()
        {
            static std::map<std::string, std::vector<std::string>, std::less<>> const sBases =
            {
                { "BaseException", {} }, { "Exception", { "BaseException" } }, { "SystemExit", { "BaseException" } },
                { "KeyboardInterrupt", { "BaseException" } }, { "GeneratorExit", { "BaseException" } },
                { "ArithmeticError", { "Exception" } }, { "ZeroDivisionError", { "ArithmeticError" } }, { "OverflowError", { "ArithmeticError" } },
                { "FloatingPointError", { "ArithmeticError" } }, { "LookupError", { "Exception" } }, { "IndexError", { "LookupError" } },
                { "KeyError", { "LookupError" } }, { "ValueError", { "Exception" } }, { "UnicodeError", { "ValueError" } },
                { "UnicodeEncodeError", { "UnicodeError" } }, { "UnicodeDecodeError", { "UnicodeError" } }, { "UnicodeTranslateError", { "UnicodeError" } },
                { "TypeError", { "Exception" } }, { "AttributeError", { "Exception" } }, { "NameError", { "Exception" } },
                { "UnboundLocalError", { "NameError" } }, { "AssertionError", { "Exception" } }, { "StopIteration", { "Exception" } },
                { "ImportError", { "Exception" } }, { "ModuleNotFoundError", { "ImportError" } }, { "RuntimeError", { "Exception" } },
                { "NotImplementedError", { "RuntimeError" } }, { "RecursionError", { "RuntimeError" } }, { "OSError", { "Exception" } },
                { "FileNotFoundError", { "OSError" } }, { "EOFError", { "Exception" } }, { "MemoryError", { "Exception" } },
                { "BlockingIOError", { "OSError" } }, { "ChildProcessError", { "OSError" } }, { "ConnectionError", { "OSError" } },
                { "BrokenPipeError", { "ConnectionError" } }, { "ConnectionAbortedError", { "ConnectionError" } }, 
                { "ConnectionRefusedError", { "ConnectionError" } }, { "ConnectionResetError", { "ConnectionError" } }, { "FileExistsError", { "OSError" } },
                { "InterruptedError", { "OSError" } }, { "IsADirectoryError", { "OSError" } }, { "NotADirectoryError", { "OSError" } },
                { "PermissionError", { "OSError" } }, { "ProcessLookupError", { "OSError" } }, { "TimeoutError", { "OSError" } },
                { "StopAsyncIteration", { "Exception" } }, { "SyntaxError", { "Exception" } }, { "IndentationError", { "SyntaxError" } },
                { "TabError", { "IndentationError" } }, { "ReferenceError", { "Exception" } },
                { "BaseExceptionGroup", { "BaseException" } }, { "ExceptionGroup", { "BaseExceptionGroup", "Exception" } },
                { "PendingDeprecationWarning", { "Warning" } }, { "SyntaxWarning", { "Warning" } }, { "FutureWarning", { "Warning" } },
                { "ImportWarning", { "Warning" } }, { "UnicodeWarning", { "Warning" } }, { "BytesWarning", { "Warning" } },
                { "ResourceWarning", { "Warning" } }, { "EncodingWarning", { "Warning" } },
                { "SystemError", { "Exception" } }, { "BufferError", { "Exception" } }, { "Warning", { "Exception" } },
                { "UserWarning", { "Warning" } }, { "DeprecationWarning", { "Warning" } }, { "RuntimeWarning", { "Warning" } },
                // (NumPy's)
                { "AxisError", { "ValueError", "IndexError" } }, { "UFuncTypeError", { "TypeError" } }, { "LinAlgError", { "ValueError" } },
                { "ComplexWarning", { "RuntimeWarning" } }, { "DTypePromotionError", { "TypeError" } }
            };
            return sBases;
        }

        // (a builtin (or module's) exception class's bases' names, and its MRO's (C3 linearization))
        std::vector<std::string> exception_base_names(std::string_view aName)
        {
            if (auto const b = exception_bases().find(aName); b != exception_bases().end())
                return b->second;
            if (auto const b = user_exception_bases().find(aName); b != user_exception_bases().end())
                return b->second;
            return {};
        }

        std::vector<std::string> exception_mro(std::string_view aName)
        {
            std::vector<std::vector<std::string>> sequences;
            auto const bases = exception_base_names(aName);
            for (auto const& b : bases)
                sequences.push_back(exception_mro(b));
            sequences.push_back(bases);
            std::vector<std::string> result{ std::string{ aName } };
            for (;;)
            {
                std::erase_if(sequences, [](auto const& seq) { return seq.empty(); });
                if (sequences.empty())
                    break;
                std::optional<std::string> candidate;
                for (auto const& seq : sequences)
                {
                    bool inTail = false;
                    for (auto const& t : sequences)
                        if (std::find(t.begin() + 1, t.end(), seq.front()) != t.end())
                            inTail = true;
                    if (!inTail)
                    {
                        candidate = seq.front();
                        break;
                    }
                }
                if (!candidate)
                    break;
                result.push_back(*candidate);
                for (auto& seq : sequences)
                    if (!seq.empty() && seq.front() == *candidate)
                        seq.erase(seq.begin());
            }
            if (result.back() != "object")
                result.push_back("object");
            return result;
        }

        std::vector<std::string> exception_subclass_names(std::string_view aName)
        {
            std::vector<std::string> result;
            for (auto const& e : exception_bases())
                if (std::find(e.second.begin(), e.second.end(), aName) != e.second.end())
                    result.push_back(e.first);
            return result;
        }

        bool is_exception_class(std::string_view aName)
        {
            return exception_bases().find(aName) != exception_bases().end() || user_exception_bases().find(aName) != user_exception_bases().end();
        }

        bool is_exception_subclass(std::string_view aClass, std::string_view aBase)
        {
            if (aClass == aBase)
                return true;
            auto bases = exception_bases().find(aClass);
            if (bases == exception_bases().end())
                bases = user_exception_bases().find(aClass);
            if (bases == exception_bases().end() || bases == user_exception_bases().end())
            {
                if (auto const user = user_exception_bases().find(aClass); user != user_exception_bases().end())
                {
                    for (auto const& b : user->second)
                        if (is_exception_subclass(b, aBase))
                            return true;
                    return false;
                }
                return aBase == "Exception" || aBase == "BaseException";    // (an exception of another kind)
            }
            for (auto const& b : bases->second)
                if (is_exception_subclass(b, aBase))
                    return true;
            return false;
        }

        object* exception_repr(neos_py_context*, object* aSelf);
        object* exception_str(neos_py_context*, object* aSelf);
        object* exception_getattr(neos_py_context*, object* aSelf, char const* aName);
        int exception_setattr(neos_py_context*, object* aSelf, char const* aName, object* aValue);

        neos_py_type make_exception_type()
        {
            auto type = make_type("python.exception", "exception", retain_object, release_object<exception_object>);
            type.repr = exception_repr;
            type.str = exception_str;
            type.getattr = exception_getattr;
            type.setattr = exception_setattr;
            return type;
        }

        neos_py_type sExceptionType = make_exception_type();

        char const* exception_type_name(object* aObject)
        {
            if (aObject == nullptr || aObject->type != &sExceptionType.base)
                return nullptr;
            return as<exception_object>(aObject)->type.c_str();
        }

        object* make_exception(std::string aType, std::vector<object*> aArgs) // (steals the arguments)
        {
            return &(new exception_object{ { { &sExceptionType.base } }, std::move(aType), std::move(aArgs) })->h.base;
        }

        object* exception_repr(neos_py_context*, object* aSelf)
        {
            auto const& e = *as<exception_object>(aSelf);
            if (e.cls != nullptr)
                if (ref const method{ special_method(aSelf, "__repr__") })
                {
                    ref const args{ make_tuple() };
                    return call(method.get(), args.get(), nullptr);
                }
            std::string text = (e.type.rfind('.') == std::string::npos ? e.type : e.type.substr(e.type.rfind('.') + 1u)) + "(";
            for (std::size_t i = 0u; i < e.args.size(); ++i)
            {
                std::string r;
                if (!repr_of(e.args[i], r))
                    return nullptr;
                text += (i != 0u ? ", " : "") + r;
            }
            return make_str(text + ")");
        }

        // (UnicodeEncodeError(encoding, object, start, end, reason) (1), UnicodeDecodeError(...) (2) and UnicodeTranslateError(object, start, end,
        // reason) (3); 0: another exception)
        int unicode_error_kind(std::string_view aType)
        {
            if (!is_exception_subclass(aType, "UnicodeError"))
                return 0;
            return is_exception_subclass(aType, "UnicodeEncodeError") ? 1 : is_exception_subclass(aType, "UnicodeDecodeError") ? 2 :
                is_exception_subclass(aType, "UnicodeTranslateError") ? 3 : 0;
        }

        // (a Unicode error's arguments (as CPython's checks them): a decode error's object is bytes)
        bool unicode_error_init(object* aException)
        {
            auto& e = *as<exception_object>(aException);
            auto const kind = unicode_error_kind(e.type);
            if (kind == 0)
                return true;
            std::size_t const count = kind == 3 ? 4u : 5u;
            if (e.args.size() != count)
                return (error("TypeError", "function takes exactly " + std::to_string(count) + " arguments (" + std::to_string(e.args.size()) + " given)"), false);
            std::size_t const objectIndex = kind == 3 ? 0u : 1u;
            auto const must_be_str = [&](std::size_t aIndex)
            {
                if (kind_of(e.args[aIndex]) == NEOS_PY_STR)
                    return true;
                error("TypeError", "argument " + std::to_string(aIndex + 1u) + " must be str, not " + type_name(e.args[aIndex]));
                return false;
            };
            if (kind != 3 && !must_be_str(0u))
                return false;
            if (kind == 2)
            {
                std::string_view data;
                if (!bytes_like(e.args[objectIndex], data))
                    return (error("TypeError", std::string{ "a bytes-like object is required, not '" } + type_name(e.args[objectIndex]) + "'"), false);
                auto const bytes = make_bytes(std::string{ data });
                release(e.args[objectIndex]);
                e.args[objectIndex] = bytes;
            }
            else if (!must_be_str(objectIndex))
                return false;
            for (auto i : { objectIndex + 1u, objectIndex + 2u })
                if (!is_int_like(e.args[i]))
                    return (error("TypeError", std::string{ "'" } + type_name(e.args[i]) + "' object cannot be interpreted as an integer"), false);
            return must_be_str(objectIndex + 3u);
        }

        // (a Unicode error's message (CPython's UnicodeEncodeError_str etc.): its attributes' values)
        bool unicode_error_text(object* aException, int aKind, std::string& aResult)
        {
            ref const object_{ getattr(aException, "object") };
            ref const startValue{ object_ ? getattr(aException, "start") : nullptr };
            ref const endValue{ startValue ? getattr(aException, "end") : nullptr };
            ref const reasonValue{ endValue ? getattr(aException, "reason") : nullptr };
            ref const encodingValue{ reasonValue ? getattr(aException, "encoding") : nullptr };
            long long start = 0, end = 0;
            std::string reason, encoding;
            if (!encodingValue || !to_index(startValue.get(), start) || !to_index(endValue.get(), end) || !str_of(reasonValue.get(), reason) ||
                (aKind != 3 && !str_of(encodingValue.get(), encoding)))
                return false;
            char buffer[64];
            if (aKind == 2)
            {
                std::string_view data;
                if (!bytes_like(object_.get(), data))
                    return (aResult.clear(), true);
                auto const length = static_cast<long long>(data.size());
                if (start >= 0 && start < length && end >= 0 && end <= length && end == start + 1)
                {
                    std::snprintf(buffer, sizeof(buffer), "byte 0x%02x in position %lld", static_cast<unsigned>(static_cast<unsigned char>(data[static_cast<std::size_t>(start)])), start);
                    aResult = "'" + encoding + "' codec can't decode " + buffer + ": " + reason;
                }
                else
                {
                    std::snprintf(buffer, sizeof(buffer), "bytes in position %lld-%lld", start, end - 1);
                    aResult = "'" + encoding + "' codec can't decode " + buffer + ": " + reason;
                }
                return true;
            }
            if (kind_of(object_.get()) != NEOS_PY_STR)
                return (aResult.clear(), true);
            auto const points = to_code_points(as<str_object>(object_.get())->value);
            auto const length = static_cast<long long>(points.size());
            std::string const prefix = aKind == 1 ? "'" + encoding + "' codec can't encode " : std::string{ "can't translate " };
            if (start >= 0 && start < length && end >= 0 && end <= length && end == start + 1)
            {
                auto const c = static_cast<unsigned>(points[static_cast<std::size_t>(start)]);
                std::snprintf(buffer, sizeof(buffer), c <= 0xFFu ? "character '\\x%02x' in position %lld" : c <= 0xFFFFu ? "character '\\u%04x' in position %lld" :
                    "character '\\U%08x' in position %lld", c, start);
            }
            else
                std::snprintf(buffer, sizeof(buffer), "characters in position %lld-%lld", start, end - 1);
            aResult = prefix + buffer + ": " + reason;
            return true;
        }

        bool exception_text(object* aException, std::string& aResult)
        {
            auto const& e = *as<exception_object>(aException);
            if (e.cls != nullptr)
                if (ref const method{ special_method(aException, "__str__") })
                {
                    ref const args{ make_tuple() };
                    ref const text{ call(method.get(), args.get(), nullptr) };
                    if (!text)
                        return false;
                    return str_of(text.get(), aResult);
                }
            if (e.args.empty())
            {
                aResult.clear();
                return true;
            }
            // (a Unicode error: CPython's message)
            if (auto const kind = unicode_error_kind(e.type); kind != 0 && e.args.size() == (kind == 3 ? 4u : 5u))
                return unicode_error_text(aException, kind, aResult);
            // (an exception group: "message (n sub-exceptions)")
            if (is_exception_subclass(e.type, "BaseExceptionGroup"))
                if (auto const exceptions = e.attributes.find("exceptions"); exceptions != nullptr && e.args.size() == 2u)
                {
                    if (!str_of(e.args[0], aResult))
                        return false;
                    auto const n = tuple_items(exceptions).size();
                    aResult += " (" + std::to_string(n) + (n == 1u ? " sub-exception)" : " sub-exceptions)");
                    return true;
                }
            // (OSError(errno, strerror[, filename[, winerror[, filename2]]]): "[Errno n] strerror: 'filename'" (Windows: a winerror: "[WinError n] ..."))
            if (e.args.size() >= 2u && kind_of(e.args[0]) == NEOS_PY_INT && is_exception_subclass(e.type, "OSError"))
            {
                std::string number, message;
                if (!str_of(e.args[0], number) || !str_of(e.args[1], message))
                    return false;
#if defined(_WIN32)
                if (e.args.size() >= 4u && kind_of(e.args[3]) == NEOS_PY_INT)
                {
                    if (!str_of(e.args[3], number))
                        return false;
                    aResult = "[WinError " + number + "] " + message;
                }
                else
#endif
                aResult = "[Errno " + number + "] " + message;
                if (e.args.size() >= 3u && kind_of(e.args[2]) != NEOS_PY_NONE && e.attributes.find("characters_written") == nullptr)
                {
                    std::string filename;
                    if (!repr_of(e.args[2], filename))
                        return false;
                    aResult += ": " + filename;
                    if (e.args.size() >= 5u && kind_of(e.args[4]) != NEOS_PY_NONE)
                    {
                        std::string filename2;
                        if (!repr_of(e.args[4], filename2))
                            return false;
                        aResult += " -> " + filename2;
                    }
                }
                return true;
            }
            // (a syntax error: "msg (file, line n)" (the file's base name: CPython's SyntaxError_str))
            if (is_exception_subclass(e.type, "SyntaxError"))
            {
                ref const msg{ getattr(aException, "msg") };
                ref const filename{ getattr(aException, "filename") };
                ref const lineno{ getattr(aException, "lineno") };
                if (!msg || !filename || !lineno)
                    return false;
                if (!str_of(msg.get(), aResult))
                    return false;
                bool const hasFile = kind_of(filename.get()) == NEOS_PY_STR;
                bool const hasLine = kind_of(lineno.get()) == NEOS_PY_INT;
                std::string file = hasFile ? as<str_object>(filename.get())->value : std::string{};
                if (auto const slash = file.find_last_of("/\\"); slash != std::string::npos)
                    file = file.substr(slash + 1u);
                std::string line;
                if (hasLine && !str_of(lineno.get(), line))
                    return false;
                if (hasFile && hasLine)
                    aResult += " (" + file + ", line " + line + ")";
                else if (hasFile)
                    aResult += " (" + file + ")";
                else if (hasLine)
                    aResult += " (line " + line + ")";
                return true;
            }
            if (e.args.size() == 1u)
            {
                // (KeyError: the repr of its key)
                if (!e.fromText && is_exception_subclass(e.type, "KeyError"))
                    return repr_of(e.args[0], aResult);
                return str_of(e.args[0], aResult);
            }
            for (auto a : e.args)
                retain(a);
            ref const t{ make_tuple(e.args) };
            return str_of(t.get(), aResult);
        }

        object* exception_str(neos_py_context*, object* aSelf)
        {
            std::string text;
            if (!exception_text(aSelf, text))
                return nullptr;
            return make_str(text);
        }

        object* exception_group_method(neos_py_context*, void* aData, object* aArgs, object*);

        object* exception_getattr(neos_py_context*, object* aSelf, char const* aName)
        {
            auto& e = *as<exception_object>(aSelf);
            if (std::strcmp(aName, "args") == 0)
            {
                for (auto a : e.args)
                    retain(a);
                return make_tuple(e.args);
            }
            if (e.cls != nullptr)
            {
                if (auto const r = user_getattr(aSelf, e.attributes, aName))
                    return r;
                if (failed())
                    return nullptr;
            }
            else if (auto const a = e.attributes.find(aName))
                return (retain(a), a);
            // (chaining, notes and traceback)
            if (std::strcmp(aName, "__cause__") == 0 || std::strcmp(aName, "__context__") == 0 || std::strcmp(aName, "__traceback__") == 0)
                return (retain(none()), none());
            if (std::strcmp(aName, "__suppress_context__") == 0)
                return make_bool(false);
            if (std::strcmp(aName, "__class__") == 0)
                return class_of(aSelf);
            if (std::strcmp(aName, "__dict__") == 0)
            {
                auto const d = e.attributes.get_dict();
                return (retain(d), d);
            }
            if (std::strcmp(aName, "add_note") == 0)
                return make_function("add_note", [](neos_py_context*, void*, object* aArgs, object*) -> object*
                {
                    auto const& args = tuple_items(aArgs);
                    if (args.size() != 2u)
                        return error("TypeError", "BaseException.add_note() takes exactly one argument (" + std::to_string(args.size() - 1u) + " given)");
                    if (kind_of(args[1]) != NEOS_PY_STR)
                        return error("TypeError", std::string{ "note must be a str, not '" } + type_name(args[1]) + "'");
                    auto& ex = *as<exception_object>(args[0]);
                    auto notes = ex.attributes.find("__notes__");
                    if (notes == nullptr)
                    {
                        ref const list{ make_list() };
                        ex.attributes.set("__notes__", list.get());
                        notes = list.get();
                    }
                    if (kind_of(notes) != NEOS_PY_LIST)
                        return error("TypeError", "Cannot add note: __notes__ is not a list");
                    as<list_object>(notes)->items.push_back((retain(args[1]), args[1]));
                    return (retain(none()), none());
                }, nullptr, aSelf);
            if (is_exception_subclass(e.type, "BaseExceptionGroup") && (std::strcmp(aName, "split") == 0 || std::strcmp(aName, "subgroup") == 0 || std::strcmp(aName, "derive") == 0))
            {
                static char const* const sNames[] = { "split", "subgroup", "derive" };
                for (auto n : sNames)
                    if (std::strcmp(n, aName) == 0)
                        return make_function(n, exception_group_method, const_cast<char*>(n), aSelf);
            }
            if (std::strcmp(aName, "with_traceback") == 0)
                return make_function("with_traceback", [](neos_py_context*, void*, object* aArgs, object*) -> object*
                {
                    auto const& args = tuple_items(aArgs);
                    if (args.size() != 2u)
                        return error("TypeError", "with_traceback() takes exactly one argument");
                    as<exception_object>(args[0])->attributes.set("__traceback__", args[1]);
                    return (retain(args[0]), args[0]);
                }, nullptr, aSelf);
            // (the attributes of builtin exceptions: their arguments)
            auto const argument = [&](std::size_t aIndex) { auto const r = aIndex < e.args.size() ? e.args[aIndex] : none(); retain(r); return r; };
            if ((std::strcmp(aName, "value") == 0 && is_exception_subclass(e.type, "StopIteration")) ||
                (std::strcmp(aName, "code") == 0 && is_exception_subclass(e.type, "SystemExit")))
                return argument(0u);
            if (is_exception_subclass(e.type, "OSError") && (std::strcmp(aName, "errno") == 0 || std::strcmp(aName, "strerror") == 0 || 
                std::strcmp(aName, "filename") == 0 || std::strcmp(aName, "filename2") == 0))
            {
                if (e.args.size() < 2u)
                    return (retain(none()), none());
                return argument(aName[0] == 'e' ? 0u : aName[0] == 's' ? 1u : aName[8] == '2' ? 4u : 2u);
            }
#if defined(_WIN32)
            // (Windows: OSError's winerror (its fourth argument))
            if (is_exception_subclass(e.type, "OSError") && std::strcmp(aName, "winerror") == 0)
                return e.args.size() >= 4u ? argument(3u) : (retain(none()), none());
#endif
            if (is_exception_subclass(e.type, "ImportError") && (std::strcmp(aName, "name") == 0 || std::strcmp(aName, "path") == 0 || 
                std::strcmp(aName, "msg") == 0 || std::strcmp(aName, "name_from") == 0))
                return std::strcmp(aName, "msg") == 0 && e.args.size() == 1u ? argument(0u) : (retain(none()), none());
            // (a syntax error's message and location: its arguments (message, (filename, lineno, offset, text[, end_lineno, end_offset])))
            if (is_exception_subclass(e.type, "SyntaxError"))
            {
                static char const* const sFields[] = { "filename", "lineno", "offset", "text", "end_lineno", "end_offset" };
                if (std::strcmp(aName, "msg") == 0)
                    return argument(0u);
                if (std::strcmp(aName, "print_file_and_line") == 0)
                    return (retain(none()), none());
                for (std::size_t i = 0u; i < std::size(sFields); ++i)
                    if (std::strcmp(aName, sFields[i]) == 0)
                    {
                        if (e.args.size() == 2u && kind_of(e.args[1]) == NEOS_PY_TUPLE && i < as<tuple_object>(e.args[1])->items.size())
                        {
                            auto const v = as<tuple_object>(e.args[1])->items[i];
                            return (retain(v), v);
                        }
                        return (retain(none()), none());
                    }
            }
            // (an attribute error's name and object, a name error's name (unless given: None))
            if ((is_exception_subclass(e.type, "AttributeError") && (std::strcmp(aName, "name") == 0 || std::strcmp(aName, "obj") == 0)) ||
                (is_exception_subclass(e.type, "NameError") && std::strcmp(aName, "name") == 0))
                return (retain(none()), none());
            // (a Unicode error's encoding, object, start, end and reason: its arguments)
            if (auto const kind = unicode_error_kind(e.type); kind != 0)
            {
                std::size_t const offset = kind == 3 ? 0u : 1u;
                if (std::strcmp(aName, "encoding") == 0)
                    return kind == 3 ? (retain(none()), none()) : argument(0u);
                if (std::strcmp(aName, "object") == 0)
                    return argument(offset);
                if (std::strcmp(aName, "start") == 0)
                    return argument(offset + 1u);
                if (std::strcmp(aName, "end") == 0)
                    return argument(offset + 2u);
                if (std::strcmp(aName, "reason") == 0)
                    return argument(offset + 3u);
            }
            return error("AttributeError", "'" + e.type + "' object has no attribute '" + aName + "'");
        }

        int exception_setattr(neos_py_context*, object* aSelf, char const* aName, object* aValue)
        {
            auto& e = *as<exception_object>(aSelf);
            if (std::strcmp(aName, "args") == 0)
            {
                for (auto a : e.args)
                    release(a);
                e.args = tuple_items(aValue);
                for (auto a : e.args)
                    retain(a);
                return 0;
            }
            return user_setattr(aSelf, e.attributes, aName, aValue);
        }

        bool exception_group_init(object* aGroup);

        object* construct_exception(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
        {
            auto const& name = *static_cast<std::string const*>(aData);
            // (the keyword arguments of attribute, name and import errors: attributes)
            std::vector<char const*> keywords;
            if (is_exception_subclass(name, "AttributeError"))
                keywords = { "name", "obj" };
            else if (is_exception_subclass(name, "NameError"))
                keywords = { "name" };
            else if (is_exception_subclass(name, "ImportError"))
                keywords = { "name", "path", "name_from" };
            auto const shortName = name.rfind('.') == std::string::npos ? name : name.substr(name.rfind('.') + 1u);
            if (aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty())
            {
                if (keywords.empty())
                    return error("TypeError", shortName + "() takes no keyword arguments");
                for (auto const& [key, value] : as<dict_object>(aKwargs)->items)
                {
                    (void)value;
                    std::string k;
                    if (!str_of(key, k))
                        return nullptr;
                    if (std::find_if(keywords.begin(), keywords.end(), [&](char const* n) { return k == n; }) == keywords.end())
                        return error("TypeError", shortName + "() got an unexpected keyword argument '" + k + "'");
                }
            }
            auto args = tuple_items(aArgs);
            for (auto a : args)
                retain(a);
            // (OSError(errno, strerror[, filename[, winerror[, filename2]]]): the subclass of its errno (Objects/exceptions.c's OSError_new))
            std::string type = name;
            if (name == "OSError" && args.size() >= 2u && args.size() <= 5u && kind_of(args[0]) == NEOS_PY_INT)
            {
                long long n = 0;
                if (to_index(args[0], n) && n >= INT_MIN && n <= INT_MAX)
                    type = errno_exception_type(static_cast<int>(n));
                else
                    clear_error();
            }
            ref e{ make_exception(type, std::move(args)) };
            if (aKwargs != nullptr)
                for (auto const& [key, value] : as<dict_object>(aKwargs)->items)
                {
                    std::string k;
                    if (str_of(key, k))
                        as<exception_object>(e.get())->attributes.set(k, value);
                }
            if (!unicode_error_init(e.get()))
                return nullptr;
            // (BlockingIOError(errno, strerror, characters_written): its third argument is the number of characters written (an integer), not a
            // filename (as CPython's OSError.__init__ has it (Objects/exceptions.c)))
            if (name == "BlockingIOError")
            {
                auto& x = *as<exception_object>(e.get());
                long long written = 0;
                if (x.args.size() == 3u && is_int_like(x.args[2]) && to_index(x.args[2], written))
                {
                    ref const characters{ make_int(written) };
                    x.attributes.set("characters_written", characters.get());
                    x.attributes.set("filename", none());
                }
                else if (failed())
                    clear_error();
            }
            if (is_exception_subclass(name, "BaseExceptionGroup") && !exception_group_init(e.get()))
                return nullptr;
            return e.release_ownership();
        }

        // --- exception groups (BaseExceptionGroup(message, exceptions) and ExceptionGroup)

        // (an exception group: its message and exceptions (attributes); BaseExceptionGroup of Exceptions is an ExceptionGroup)
        bool exception_group_init(object* aGroup)
        {
            auto& g = *as<exception_object>(aGroup);
            if (g.args.size() != 2u)
            {
                error("TypeError", "BaseExceptionGroup.__new__() takes exactly 2 arguments (" + std::to_string(g.args.size()) + " given)");
                return false;
            }
            if (kind_of(g.args[0]) != NEOS_PY_STR)
            {
                error("TypeError", std::string{ "argument 1 must be str, not " } + type_name(g.args[0]));
                return false;
            }
            std::vector<object*> items;
            auto const k = kind_of(builtin_value(g.args[1]));
            if (k != NEOS_PY_LIST && k != NEOS_PY_TUPLE)
            {
                error("TypeError", "second argument (exceptions) must be a sequence");
                return false;
            }
            if (!collect(g.args[1], items))
                return false;
            ref const exceptions{ make_tuple(items) };
            if (items.empty())
            {
                error("ValueError", "second argument (exceptions) must be a non-empty sequence");
                return false;
            }
            bool allExceptions = true;
            for (std::size_t i = 0u; i < items.size(); ++i)
            {
                auto const itemType = exception_type_name(items[i]);
                if (itemType == nullptr)
                {
                    error("ValueError", "Item " + std::to_string(i) + " of second argument (exceptions) is not an exception");
                    return false;
                }
                if (!is_exception_subclass(itemType, "Exception"))
                    allExceptions = false;
            }
            if (!allExceptions && is_exception_subclass(g.type, "ExceptionGroup"))
            {
                error("TypeError", "Cannot nest BaseExceptions in an ExceptionGroup");
                return false;
            }
            if (allExceptions && g.type == "BaseExceptionGroup" && g.cls == nullptr)
                g.type = "ExceptionGroup";
            g.attributes.set("message", g.args[0]);
            g.attributes.set("exceptions", exceptions.get());
            return true;
        }

        bool is_exception_group(object* aObject)
        {
            auto const name = exception_type_name(aObject);
            return name != nullptr && is_exception_subclass(name, "BaseExceptionGroup");
        }

        // (an exception matches a split's condition: an exception class, a tuple of them, or a predicate)
        int exception_condition(object* aException, object* aCondition)
        {
            if (is_type_object(aCondition) || is_class(aCondition) || kind_of(aCondition) == NEOS_PY_TUPLE)
            {
                ref const is{ b_isinstance({ aException, aCondition }, nullptr) };
                return is ? truth(is.get()) : -1;
            }
            ref const args{ make_tuple({ (retain(aException), aException) }) };
            ref const r{ call(aCondition, args.get(), nullptr) };
            return r ? truth(r.get()) : -1;
        }

        // (group.derive(exceptions) with the group's traceback, cause, context and notes)
        object* exception_group_derive(object* aGroup, std::vector<object*> aExceptions, bool aCopy)
        {
            ref derive{ getattr(aGroup, "derive") };
            if (!derive)
                return nullptr;
            ref const list{ make_list(std::move(aExceptions)) };
            ref const args{ make_tuple({ (retain(list.get()), list.get()) }) };
            ref result{ call(derive.get(), args.get(), nullptr) };
            if (!result || !aCopy || exception_type_name(result.get()) == nullptr)
                return result.release_ownership();
            auto& from = *as<exception_object>(aGroup);
            auto& to = *as<exception_object>(result.get());
            for (auto const name : { "__traceback__", "__cause__", "__context__", "__notes__" })
                if (auto const value = from.attributes.find(name))
                    to.attributes.set(name, value);
            return result.release_ownership();
        }

        // (group.split(condition): (match, rest) (aSubgroup: match only))
        bool exception_group_split(object* aGroup, object* aCondition, ref& aMatch, ref& aRest)
        {
            auto const whole = exception_condition(aGroup, aCondition);
            if (whole < 0)
                return false;
            if (whole == 1)
            {
                aMatch = ref::borrowed(aGroup);
                aRest = ref{};
                return true;
            }
            auto const exceptions = as<exception_object>(aGroup)->attributes.find("exceptions");
            std::vector<object*> matched, rest;
            for (auto e : exceptions != nullptr ? tuple_items(exceptions) : std::vector<object*>{})
            {
                if (is_exception_group(e))
                {
                    ref m, r;
                    if (!exception_group_split(e, aCondition, m, r))
                        return false;
                    if (m)
                        matched.push_back(m.release_ownership());
                    if (r)
                        rest.push_back(r.release_ownership());
                    continue;
                }
                auto const c = exception_condition(e, aCondition);
                if (c < 0)
                    return false;
                (c == 1 ? matched : rest).push_back((retain(e), e));
            }
            if (!matched.empty())
            {
                aMatch = ref{ exception_group_derive(aGroup, std::move(matched), true) };
                if (!aMatch)
                    return false;
            }
            if (!rest.empty())
            {
                aRest = ref{ exception_group_derive(aGroup, std::move(rest), true) };
                if (!aRest)
                    return false;
            }
            return true;
        }

        // (a try statement with except* clauses (see the Python compiler's statements.cpp): the exception that its body raised
        // (as an exception group), what remains of it once its clauses have matched it)
        struct exception_star_object
        {
            header h;
            object* original = nullptr;     ///< (the exception raised)
            object* remaining = nullptr;    ///< (a group)
            bool matched = false;
            ~exception_star_object() { release(original); release(remaining); }
        };

        struct exception_star_match_object
        {
            header h;
            object* state;
            object* condition;
            bool last;
            int step = 0;
            ~exception_star_match_object() { release(state); release(condition); }
        };

        neos_py_type make_exception_star_type();
        neos_py_type make_exception_star_match_type();
        neos_py_type sExceptionStarType = make_exception_star_type();
        neos_py_type sExceptionStarMatchType = make_exception_star_match_type();

        object* exception_star_method(neos_py_context*, void* aData, object* aArgs, object*)
        {
            auto const& args = tuple_items(aArgs);
            auto& state = *reinterpret_cast<exception_star_object*>(args.at(0u));
            std::string_view const name{ static_cast<char const*>(aData) };
            if (name == "__enter__")
                return (retain(args[0]), args[0]);
            if (name == "__exit__")
            {
                if (args.size() < 3u || kind_of(args[2]) == NEOS_PY_NONE)
                    return make_bool(false);
                auto const e = args[2];
                retain(e);
                state.original = e;
                if (is_exception_group(e))
                    state.remaining = (retain(e), e);
                else
                {
                    ref const cls{ fn_builtin("BaseExceptionGroup") };
                    ref list{ make_tuple({ (retain(e), e) }) };
                    ref const a{ make_tuple({ make_str(""), list.release_ownership() }) };
                    state.remaining = call(cls.get(), a.get(), nullptr);
                    if (state.remaining == nullptr)
                        return nullptr;
                }
                return make_bool(true);
            }
            if (name == "none")
                return make_bool(state.original == nullptr);
            if (name == "match")
            {
                if (args.size() != 3u)
                    return error("SystemError", "match(condition, last)");
                // (except* ExceptionGroup is not allowed)
                auto const check = [](object* c) { return (is_type_object(c) || is_class(c)) && exception_type_name(c) == nullptr; };
                (void)check;
                auto* const m = new exception_star_match_object{ { { &sExceptionStarMatchType.base } }, (retain(args[0]), args[0]), (retain(args[1]), args[1]), truth(args[2]) == 1 };
                return &m->h.base;
            }
            return error("AttributeError", std::string{ name });
        }

        neos_py_type make_exception_star_type()
        {
            auto type = make_type("exception_star", "exception_star", retain_object, release_object<exception_star_object>);
            type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
            {
                static char const* const sNames[] = { "__enter__", "__exit__", "none", "match" };
                for (auto n : sNames)
                    if (std::strcmp(n, aName) == 0)
                        return make_function(n, exception_star_method, const_cast<char*>(n), aSelf);
                return error("AttributeError", std::string{ "'exception_star' object has no attribute '" } + aName + "'");
            };
            return type;
        }

        neos_py_type make_exception_star_match_type()
        {
            auto type = make_type("exception_star_match", "exception_star_match", retain_object, release_object<exception_star_match_object>);
            type.iter = [](neos_py_context*, object* aSelf) -> object* { retain(aSelf); return aSelf; };
            type.next = [](neos_py_context*, object* aSelf) -> object*
            {
                auto& m = *reinterpret_cast<exception_star_match_object*>(aSelf);
                auto& state = *reinterpret_cast<exception_star_object*>(m.state);
                if (m.step++ == 0 && state.remaining != nullptr)
                {
                    if (is_type_object(m.condition) || is_class(m.condition))
                    {
                        ref const is{ b_issubclass({ m.condition, ref{ fn_builtin("BaseExceptionGroup") }.get() }, nullptr) };
                        if (is && truth(is.get()) == 1)
                            return error("TypeError", "catching ExceptionGroup with except* is not allowed. Use except instead.");
                        clear_error();
                    }
                    // (an exception that is not a group: the group of it, if it matches)
                    if (!is_exception_group(state.original) && !state.matched)
                    {
                        auto const c = exception_condition(state.original, m.condition);
                        if (c < 0)
                            return nullptr;
                        if (c == 1)
                        {
                            auto const group = state.remaining;
                            state.remaining = nullptr;
                            state.matched = true;
                            return group;
                        }
                    }
                    ref match, rest;
                    if (!exception_group_split(state.remaining, m.condition, match, rest))
                        return nullptr;
                    if (match)
                    {
                        release(state.remaining);
                        state.remaining = rest.release_ownership();
                        state.matched = true;
                        return match.release_ownership();
                    }
                }
                // (the last clause: what remains is raised (the exception itself if no clause matched it))
                if (m.last && state.remaining != nullptr && m.step <= 2)
                {
                    m.step = 3;
                    fn_raise(state.matched ? state.remaining : state.original);
                    return nullptr;
                }
                return nullptr;
            };
            return type;
        }

        object* b_exception_group(std::vector<object*> const&, object*)
        {
            return &(new exception_star_object{ { { &sExceptionStarType.base } } })->h.base;
        }

        object* exception_group_method(neos_py_context*, void* aData, object* aArgs, object*)
        {
            auto const& args = tuple_items(aArgs);
            std::string_view const name{ static_cast<char const*>(aData) };
            if (args.size() != 2u)
                return error("TypeError", std::string{ "BaseExceptionGroup." } + std::string{ name } + "() takes exactly one argument (" + std::to_string(args.size() - 1u) + " given)");
            auto const self = args[0];
            if (name == "derive")
            {
                auto const message = as<exception_object>(self)->attributes.find("message");
                ref const cls{ fn_builtin("BaseExceptionGroup") };
                ref const a{ make_tuple({ (retain(message), message), (retain(args[1]), args[1]) }) };
                return call(cls.get(), a.get(), nullptr);
            }
            ref match, rest;
            if (!exception_group_split(self, args[1], match, rest))
                return nullptr;
            auto const value = [](ref& r) { return r ? r.release_ownership() : (retain(none()), none()); };
            if (name == "subgroup")
                return value(match);
            return make_tuple({ value(match), value(rest) });
        }

        int exception_instancecheck(neos_py_context*, void* aData, object* aObject)
        {
            auto const name = exception_type_name(aObject);
            return name != nullptr && is_exception_subclass(name, *static_cast<std::string const*>(aData)) ? 1 : 0;
        }

        object* exception_class(std::string const& aName)
        {
            static std::map<std::string, std::string, std::less<>> sNames;
            auto& name = sNames[aName];
            name = aName;
            // (a module's exception (e.g. decimal.InvalidOperation): its name is the last part)
            auto const dot = aName.rfind('.');
            auto const result = type_object_for(dot == std::string::npos ? aName : aName.substr(dot + 1u), aName);
            as<type_object>(result)->call = construct_exception;
            as<type_object>(result)->data = &name;
            as<type_object>(result)->instancecheck = exception_instancecheck;
            return result;
        }

        // (the exception last raised by a raise statement, and the one last caught)
        thread_local object* tRaised = nullptr;
        thread_local std::string tRaisedText;
        thread_local object* tCaught = nullptr;
        thread_local bool tPropagating = false;     ///< (fn_caught: the exception propagates once it is handled (see frames_caught))
        thread_local std::vector<object*> tHandling;   // (the exceptions being handled: except clauses can nest)
        // (the object whose attribute was not found (an attribute error raised as text: its obj), and that error's text)
        thread_local object* tErrorSubject = nullptr;
        thread_local std::string tErrorSubjectText;

        void note_attribute_error(object* aObject)
        {
            if (!failed() || !std::string_view{ error_text() }.starts_with("AttributeError: "))
                return;
            // (the previous subject is released last: its release can run a __del__ that notes an AttributeError of its own)
            retain(aObject);
            auto const previous = tErrorSubject;
            tErrorSubject = aObject;
            tErrorSubjectText = error_text();
            release(previous);
        }

        void raise_exception(object* aException, bool aContext)
        {
            // (raised while another is being handled: that one is its context)
            if (aContext && tCaught != nullptr && tCaught != aException && aException->type == &sExceptionType.base)
            {
                auto& attributes = as<exception_object>(aException)->attributes;
                bool cycle = false;
                for (auto c = tCaught; c != nullptr && c->type == &sExceptionType.base; c = as<exception_object>(c)->attributes.find("__context__"))
                    if (c == aException)
                    {
                        cycle = true;
                        break;
                    }
                if (!cycle)
                    attributes.set("__context__", tCaught);
            }
            if (aException->type == &sExceptionType.base && as<exception_object>(aException)->attributes.find("__traceback__") == nullptr)
            {
                ref const traceback{ make_traceback() };
                as<exception_object>(aException)->attributes.set("__traceback__", traceback.get());
            }
            std::string message;
            if (!exception_text(aException, message))
                return;
            auto const& type = as<exception_object>(aException)->type;
            retain(aException);
            release(tRaised);
            tRaised = aException;
            tRaisedText = message.empty() ? type : type + ": " + message;
            python_runtime::raise(type, message);
        }

        void raise_exception(object* aException)
        {
            raise_exception(aException, true);
        }

        void reraise_exception(object* aException)
        {
            if (exception_type_name(aException) != nullptr)
                raise_exception(aException, false);
            else
                fn_raise(aException);
        }

        // (KeyError(key))
        void raise_key_error(object* aKey)
        {
            retain(aKey);
            ref const e{ make_exception("KeyError", { aKey }) };
            raise_exception(e.get());
        }

        // (native division by zero)
        void fn_zero_division(std::int32_t aKind)
        {
            static char const* const sMessages[] = { "division by zero", "float division by zero", "float floor division by zero", "float modulo by zero",
                "integer division or modulo by zero", "integer modulo by zero" };
            error("ZeroDivisionError", sMessages[aKind >= 0 && aKind < 6 ? aKind : 0]);
        }

        // raise exception
        object* call(object* aCallable, object* aArgs, object* aKwargs);

        void fn_raise(object* aException)
        {
            if (exception_type_name(aException) != nullptr)
                raise_exception(aException);
            else if (is_class(aException) && is_exception_class(as_class_name(aException)))
            {
                ref const args{ make_tuple() };
                ref const e{ call(aException, args.get(), nullptr) };
                if (e)
                    fn_raise(e.get());
            }
            else if (is_type_object(aException) && is_exception_class(as<type_object>(aException)->qualifiedName))
            {
                ref const e{ make_exception(as<type_object>(aException)->qualifiedName, {}) };
                raise_exception(e.get());
            }
            else
                error("TypeError", "exceptions must derive from BaseException");
        }

        // (an exception or an exception class: the exception (a class is called))
        ref exception_instance(object* aException)
        {
            if (exception_type_name(aException) != nullptr)
                return ref::borrowed(aException);
            if ((is_class(aException) && is_exception_class(as_class_name(aException))) ||
                (is_type_object(aException) && is_exception_class(as<type_object>(aException)->qualifiedName)))
            {
                ref const args{ make_tuple() };
                return ref{ call(aException, args.get(), nullptr) };
            }
            error("TypeError", "exceptions must derive from BaseException");
            return ref{};
        }

        // raise exception from cause
        void fn_raise_from(object* aException, object* aCause)
        {
            auto const e = exception_instance(aException);
            if (!e)
                return;
            ref cause;
            if (kind_of(aCause) != NEOS_PY_NONE)
            {
                cause = exception_instance(aCause);
                if (!cause)
                {
                    clear_error();
                    error("TypeError", "exception causes must derive from BaseException");
                    return;
                }
            }
            auto& attributes = as<exception_object>(e.get())->attributes;
            attributes.set("__cause__", cause ? cause.get() : none());
            attributes.set("__suppress_context__", make_bool(true));
            raise_exception(e.get());
        }

        // (a raise statement's: raise exception (from cause))
        void fn_raise_statement(object* aException)
        {
            explicit_raise();
            fn_raise(aException);
        }

        void fn_raise_from_statement(object* aException, object* aCause)
        {
            explicit_raise();
            fn_raise_from(aException, aCause);
        }

        // raise (the exception being handled)
        void fn_reraise()
        {
            if (tCaught == nullptr)
                error("RuntimeError", "No active exception to reraise");
            else
                raise_exception(tCaught);
        }

        // the exception being handled (from its "type: message")
        object* fn_caught(char const* aText)
        {
            std::string const text = aText != nullptr ? aText : "";
            object* result = nullptr;
            if (tRaised != nullptr && text == tRaisedText)
            {
                retain(tRaised);
                result = tRaised;
            }
            else
            {
                auto const colon = text.find(": ");
                auto const type = text.substr(0u, colon);
                std::vector<object*> args;
                if (colon != std::string::npos)
                    args.push_back(make_str(text.substr(colon + 2u)));
                result = make_exception(type, std::move(args));
                as<exception_object>(result)->fromText = true;
                // (the name of a name or attribute error (and the object of the latter): from its message)
                auto const quoted = [&](std::string_view aBefore, std::string_view aAfter) -> std::optional<std::string>
                {
                    std::string_view const message = colon != std::string::npos ? std::string_view{ text }.substr(colon + 2u) : std::string_view{};
                    auto const start = message.find(aBefore);
                    if (start == std::string_view::npos)
                        return std::nullopt;
                    auto const end = message.find(aAfter, start + aBefore.size());
                    if (end == std::string_view::npos)
                        return std::nullopt;
                    return std::string{ message.substr(start + aBefore.size(), end - start - aBefore.size()) };
                };
                auto& attributes = as<exception_object>(result)->attributes;
                {
                    ref const traceback{ make_traceback() };
                    attributes.set("__traceback__", traceback.get());
                }
                if (type == "NameError" || type == "UnboundLocalError")
                {
                    if (auto const name = quoted(type == "NameError" ? "name '" : "local variable '", "'"))
                    {
                        ref const n{ make_str(*name) };
                        attributes.set("name", n.get());
                    }
                }
                else if (type == "AttributeError")
                {
                    if (auto const name = quoted("has no attribute '", "'"))
                    {
                        ref const n{ make_str(*name) };
                        attributes.set("name", n.get());
                    }
                    if (tErrorSubject != nullptr && text == tErrorSubjectText)
                        attributes.set("obj", tErrorSubject);
                }
                else if (type == "ImportError" || type == "ModuleNotFoundError")
                {
                    auto const set = [&](char const* aAttribute, std::optional<std::string> const& aValue)
                    {
                        if (aValue)
                        {
                            ref const v{ make_str(*aValue) };
                            attributes.set(aAttribute, v.get());
                        }
                    };
                    if (auto const from = quoted("cannot import name '", "'"))
                    {
                        set("name_from", from);
                        set("name", quoted("' from '", "'"));
                    }
                    else
                        set("name", quoted("No module named '", "'"));
                }
            }
            if (tErrorSubject != nullptr)
            {
                release(tErrorSubject);
                tErrorSubject = nullptr;
                tErrorSubjectText.clear();
            }
            // (its traceback: the frames it passed through)
            if (result->type == &sExceptionType.base)
            {
                auto& attributes = as<exception_object>(result)->attributes;
                if (ref const traceback{ frames_caught(attributes.find("__traceback__"), text, tPropagating) })
                    attributes.set("__traceback__", traceback.get());
            }
            retain(result);
            tHandling.push_back(tCaught);
            tCaught = result;
            return result;
        }

        // (the handler of the exception being handled has finished)
        void fn_handled()
        {
            release(tCaught);
            tCaught = nullptr;
            if (!tHandling.empty())
            {
                tCaught = tHandling.back();
                tHandling.pop_back();
            }
        }

        object* handled_exception()
        {
            return tCaught;
        }

        // except class (or tuple of classes)
        bool fn_exception_matches(object* aException, object* aClass)
        {
            if (kind_of(aClass) == NEOS_PY_TUPLE)
            {
                for (auto c : as<tuple_object>(aClass)->items)
                    if (fn_exception_matches(aException, c))
                        return true;
                return false;
            }
            if (is_class(aClass))
            {
                if (!is_exception_class(as_class_name(aClass)))
                {
                    error("TypeError", "catching classes that do not inherit from BaseException is not allowed");
                    return false;
                }
                auto const cls = user_class_of(aException);
                return cls != nullptr && is_subclass(cls, aClass);
            }
            if (!is_type_object(aClass) || !is_exception_class(as<type_object>(aClass)->qualifiedName))
            {
                if (is_type_object(aClass) && as<type_object>(aClass)->instancecheck != nullptr)
                    return as<type_object>(aClass)->instancecheck(context(), as<type_object>(aClass)->data, aException) == 1;
                error("TypeError", "catching classes that do not inherit from BaseException is not allowed");
                return false;
            }
            auto const name = exception_type_name(aException);
            return name != nullptr && is_exception_subclass(name, as<type_object>(aClass)->qualifiedName);
        }

        // with manager: manager.__enter__() and manager.__exit__(...)
        object* call_method_named(object* aObject, char const* aName, std::vector<object*> aArgs)
        {
            ref const method{ getattr(aObject, aName) };
            if (!method)
                return nullptr;
            ref const args{ make_tuple(std::move(aArgs)) };
            return call(method.get(), args.get(), nullptr);
        }
        object* fn_with_enter(object* aManager)
        {
            return call_method_named(aManager, "__enter__", {});
        }
        void fn_with_exit(object* aManager)
        {
            ref const r{ call_method_named(aManager, "__exit__", { none(), none(), none() }) };
        }
        bool fn_with_exit_exception(object* aManager, char const* aText)
        {
            tPropagating = true;
            ref const exception{ fn_caught(aText) };
            tPropagating = false;
            ref const type{ class_of(exception.get()) };
            ref traceback{ getattr(exception.get(), "__traceback__") };
            if (!traceback)
            {
                clear_error();
                traceback = ref{ (retain(none()), none()) };
            }
            retain(type.get());
            retain(exception.get());
            ref const r{ call_method_named(aManager, "__exit__", { type.get(), exception.get(), traceback.release_ownership() }) };
            fn_handled();
            if (!r)
                return false;
            return truth(r.get()) == 1;
        }

        // (an exception as CPython reports it when the program does not handle it: its chain (causes and contexts), then 
        // "Traceback (most recent call last):", then "type: message" (and its notes))
        bool traceback_report(object* aException, bool aColorize, std::string& aResult);

        std::string exception_report(object* aException, std::set<object*>& aSeen, bool aColorize)
        {
            // (as the module traceback formats it, if it can)
            if (aSeen.empty())
                if (std::string report; traceback_report(aException, aColorize, report))
                    return report;
            if (aException->type != &sExceptionType.base || !aSeen.insert(aException).second)
                return {};
            auto& e = *as<exception_object>(aException);
            std::string result;
            auto const cause = e.attributes.find("__cause__");
            auto const context = e.attributes.find("__context__");
            auto const suppress = e.attributes.find("__suppress_context__");
            if (cause != nullptr && kind_of(cause) != NEOS_PY_NONE)
                result += exception_report(cause, aSeen) + "\nThe above exception was the direct cause of the following exception:\n\n";
            else if (context != nullptr && kind_of(context) != NEOS_PY_NONE && !(suppress != nullptr && truth(suppress) == 1))
                result += exception_report(context, aSeen) + "\nDuring handling of the above exception, another exception occurred:\n\n";
            result += "Traceback (most recent call last):\n";
            std::string name = e.type;
            if (e.cls != nullptr)
            {
                ref const qualname{ getattr(e.cls, "__qualname__") };
                ref const module{ getattr(e.cls, "__module__") };
                if (qualname && module && kind_of(qualname.get()) == NEOS_PY_STR && kind_of(module.get()) == NEOS_PY_STR)
                {
                    auto const& m = as<str_object>(module.get())->value;
                    name = (m == "__main__" || m == "builtins" ? std::string{} : m + ".") + as<str_object>(qualname.get())->value;
                }
                clear_error();
            }
            std::string message;
            if (!exception_text(aException, message))
            {
                clear_error();
                message = "<exception str() failed>";
            }
            result += message.empty() ? name : name + ": " + message;
            result += "\n";
            if (auto const notes = e.attributes.find("__notes__"); notes != nullptr && kind_of(notes) == NEOS_PY_LIST)
                for (auto n : as<list_object>(notes)->items)
                {
                    std::string text;
                    if (str_of(n, text))
                        result += text + "\n";
                    else
                        clear_error();
                }
            return result;
        }

        // (an exception that the program does not handle (its text): reported on the standard error stream; the status the 
        // program exits with (SystemExit: its code))
        std::int32_t unhandled_status(char const* aText);

        std::int32_t fn_unhandled(char const* aText)
        {
            auto const status = unhandled_status(aText);
            threads_end();
            run_exit_functions();
            return status;
        }

        std::int32_t unhandled_status(char const* aText)
        {
            if (ref const out{ sys_stdout() })
            {
                ref const r{ call_method_named(out.get(), "flush", {}) };
                clear_error();
            }
            flush_standard(1);
            std::string const text = aText != nullptr ? aText : "";
            clear_error();
            ref const e{ fn_caught(text.c_str()) };
            fn_handled();
            clear_error();
            auto const type = exception_type_name(e.get());
            if (type != nullptr && is_exception_subclass(type, "SystemExit"))
            {
                auto const& args = as<exception_object>(e.get())->args;
                if (args.empty() || kind_of(args[0]) == NEOS_PY_NONE)
                    return 0;
                if (args.size() == 1u && is_int_like(args[0]))
                {
                    long long code = 0;
                    if (to_index(args[0], code))
                        return static_cast<std::int32_t>(code);
                    clear_error();
                    return 1;
                }
                std::string message;
                if (args.size() == 1u ? !str_of(args[0], message) : !exception_text(e.get(), message))
                    clear_error();
                write_standard(2, message + "\n");
                return 1;
            }
            // (sys.excepthook, if the program has replaced it)
            if (modules_imported("sys"))
            {
                ref const hook{ module_attribute("sys", "excepthook") };
                ref const original{ module_attribute("sys", "__excepthook__") };
                clear_error();
                if (hook && hook.get() != original.get())
                {
                    ref const traceback{ getattr(e.get(), "__traceback__") };
                    clear_error();
                    ref const r{ invoke(hook.get(), { class_of(e.get()), new_reference(e.get()), new_reference(traceback ? traceback.get() : none()) }) };
                    if (!r)
                    {
                        std::string const hookText = error_text();
                        clear_error();
                        ref const hookError{ fn_caught(hookText.c_str()) };
                        fn_handled();
                        clear_error();
                        std::set<object*> hookSeen;
                        std::set<object*> seen;
                        write_standard(2, "Error in sys.excepthook:\n" + exception_report(hookError.get(), hookSeen) + "\nOriginal exception was:\n" + 
                            exception_report(e.get(), seen));
                    }
                    if (ref const err{ module_attribute("sys", "stderr") })
                    {
                        ref const f{ call_method_named(err.get(), "flush", {}) };
                    }
                    clear_error();
                    flush_standard(2);
                    return 1;
                }
            }
            std::set<object*> seen;
            write_standard(2, exception_report(e.get(), seen, traceback_colorize()));
            return 1;
        }

        // assert condition, message
        void fn_assertion(object* aMessage)
        {
            std::vector<object*> args;
            if (aMessage != nullptr && kind_of(aMessage) != NEOS_PY_NONE)
            {
                retain(aMessage);
                args.push_back(aMessage);
            }
            ref const e{ make_exception("AssertionError", std::move(args)) };
            raise_exception(e.get());
        }

        // --- builtins (the functions and classes of Python's builtins module)

        object* call_builtin(neos_py_context*, void* aData, neos_py_object* aArgs, neos_py_object* aKwargs)
        {
            auto const* entry = static_cast<builtin_entry const*>(aData);
            auto const& args = tuple_items(aArgs);
            return entry->function(args, aKwargs != nullptr && as<dict_object>(aKwargs)->items.empty() ? nullptr : aKwargs);
        }

        // keyword arguments (each must be one of aAllowed)
        bool keywords(object* aKwargs, std::initializer_list<std::pair<char const*, object**>> aAllowed, char const* aFunction)
        {
            if (aKwargs == nullptr)
                return true;
            for (auto const& item : as<dict_object>(aKwargs)->items)
            {
                auto const& key = as<str_object>(item.first)->value;
                bool found = false;
                for (auto const& allowed : aAllowed)
                    if (key == allowed.first)
                    {
                        *allowed.second = item.second;
                        found = true;
                    }
                if (!found)
                {
                    error("TypeError", "'" + key + "' is an invalid keyword argument for " + aFunction + "()");
                    return false;
                }
            }
            return true;
        }

        bool no_keywords(object* aKwargs, char const* aFunction)
        {
            if (aKwargs == nullptr || as<dict_object>(aKwargs)->items.empty())
                return true;
            error("TypeError", std::string{ aFunction } + "() takes no keyword arguments");
            return false;
        }

        namespace
        {
            neos_rt_host const* standard_host()
            {
                auto const* h = python_runtime::host();
                return h != nullptr && h->version >= 9u && h->std_write != nullptr ? h : nullptr;
            }
        }

        bool write_standard(int aStream, std::string_view aText)
        {
            if (auto const* h = standard_host())
                return h->std_write(aStream, aText.data(), aText.size()) == aText.size() && h->std_flush(aStream) == 0;
            auto* const stream = aStream == 2 ? stderr : stdout;
            return std::fwrite(aText.data(), 1u, aText.size(), stream) == aText.size() && std::fflush(stream) == 0;
        }

        int flush_standard(int aStream)
        {
            if (auto const* h = standard_host())
                return h->std_flush(aStream);
            return std::fflush(aStream == 0 ? stdin : aStream == 1 ? stdout : stderr);
        }

        std::size_t read_standard(char* aBuffer, std::size_t aCount)
        {
            if (auto const* h = standard_host())
                return h->std_read(0, aBuffer, aCount);
            return std::fread(aBuffer, 1u, aCount, stdin);
        }

        int getc_standard()
        {
            if (auto const* h = standard_host())
                return h->std_getc(0);
            return std::fgetc(stdin);
        }

        int ungetc_standard(int aCharacter)
        {
            if (auto const* h = standard_host())
                return h->std_ungetc(aCharacter, 0);
            return std::ungetc(aCharacter, stdin);
        }

        std::size_t read_standard_some(char* aBuffer, std::size_t aCount)
        {
            std::size_t n = 0u;
            for (int c = n < aCount ? getc_standard() : EOF; c != EOF; c = n < aCount ? getc_standard() : EOF)
            {
                aBuffer[n++] = static_cast<char>(c);
                if (c == '\n')
                    break;
            }
            return n;
        }

        bool read_standard_line(std::string& aLine)
        {
            aLine.clear();
            for (int c = getc_standard(); c != EOF; c = getc_standard())
            {
                aLine += static_cast<char>(c);
                if (c == '\n')
                    return true;
            }
            return !aLine.empty();
        }

        bool write_out(std::string const& aText)
        {
            // (sys.stdout, if the program has replaced it (e.g. contextlib.redirect_stdout))
            if (ref const out{ sys_stdout() }; out && !is_standard_output(out.get()))
            {
                if (kind_of(out.get()) == NEOS_PY_NONE)
                    return true;
                ref const r{ call_method_named(out.get(), "write", { make_str(aText) }) };
                return static_cast<bool>(r);
            }
            // (an error writing (e.g. EPIPE: the reader has gone, SIGPIPE being ignored) is OSError, as CPython's)
            if (!write_standard(1, aText))
            {
                int const e = errno;
                std::clearerr(stdout);
                raise_os_error(e);
                return false;
            }
            return true;
        }

        object* b_print(std::vector<object*> const& aArgs, object* aKwargs)
        {
            object* sep = nullptr;
            object* end = nullptr;
            object* file = nullptr;
            object* flush = nullptr;
            if (!keywords(aKwargs, { { "sep", &sep }, { "end", &end }, { "file", &file }, { "flush", &flush } }, "print"))
                return nullptr;
            auto const text_of = [](object* aValue, char const* aDefault, char const* aWhat, std::string& aResult)
            {
                if (aValue == nullptr || kind_of(aValue) == NEOS_PY_NONE)
                    aResult = aDefault;
                else if (kind_of(aValue) == NEOS_PY_STR)
                    aResult = as<str_object>(aValue)->value;
                else
                {
                    error("TypeError", std::string{ aWhat } + " must be None or a string, not " + type_name(aValue));
                    return false;
                }
                return true;
            };
            std::string separator, terminator;
            if (!text_of(sep, " ", "sep", separator) || !text_of(end, "\n", "end", terminator))
                return nullptr;
            std::string line;
            for (std::size_t i = 0u; i < aArgs.size(); ++i)
            {
                std::string text;
                if (!str_of(aArgs[i], text))
                    return nullptr;
                if (i != 0u)
                    line += separator;
                line += text;
            }
            // (the file (default: sys.stdout))
            ref target;
            if (file != nullptr && kind_of(file) != NEOS_PY_NONE)
                target = ref::borrowed(file);
            else if (auto const stdout_ = sys_stdout())
                target = ref{ stdout_ };
            if (!target || is_standard_output(target.get()))
            {
                if (!write_out(line + terminator))
                    return nullptr;
                return none();
            }
            ref const text{ make_str(line + terminator) };
            ref const r{ call_method_named(target.get(), "write", { (retain(text.get()), text.get()) }) };
            if (!r)
                return nullptr;
            if (flush != nullptr && truth(flush) == 1)
            {
                ref const f{ call_method_named(target.get(), "flush", {}) };
                if (!f)
                    return nullptr;
            }
            return none();
        }

        object* b_len(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "len") || !arity(aArgs, 1u, 1u, "len"))
                return nullptr;
            auto const n = length(aArgs[0]);
            return n < 0 ? nullptr : make_int(n);
        }

        object* b_repr(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "repr") || !arity(aArgs, 1u, 1u, "repr"))
                return nullptr;
            std::string text;
            return repr_of(aArgs[0], text) ? make_str(std::move(text)) : nullptr;
        }

        object* b_str(std::vector<object*> const& aArgs, object* aKwargs)
        {
            // str(object='') and str(object=b'', encoding='utf-8', errors='strict')
            if (aArgs.size() > 3u)
                return error("TypeError", "str expected at most 3 arguments, got " + std::to_string(aArgs.size()));
            std::vector<object*> a;
            if (!parse_arguments("str", aArgs, aKwargs, { "object", "encoding", "errors" }, 0u, 0u, a))
                return nullptr;
            if (a[1] != nullptr || a[2] != nullptr)
            {
                std::string encoding = "utf-8", errors = "strict";
                for (std::size_t i = 1u; i < 3u; ++i)
                    if (a[i] != nullptr)
                    {
                        if (kind_of(a[i]) != NEOS_PY_STR)
                            return error("TypeError", std::string{ "str() argument '" } + (i == 1u ? "encoding" : "errors") + "' must be str, not " + type_name(a[i]));
                        (i == 1u ? encoding : errors) = as<str_object>(a[i])->value;
                    }
                if (a[0] == nullptr)
                    return make_str({});
                std::string_view data;
                if (kind_of(a[0]) == NEOS_PY_STR)
                    return error("TypeError", "decoding str is not supported");
                if (!bytes_like(a[0], data))
                    return error("TypeError", std::string{ "decoding to str: need a bytes-like object, " } + type_name(a[0]) + " found");
                std::string text;
                return decode(data, encoding, errors, text) ? make_str(std::move(text)) : nullptr;
            }
            if (a[0] == nullptr)
                return make_str({});
            std::string text;
            return str_of(a[0], text) ? make_str(std::move(text)) : nullptr;
        }

        std::int64_t fn_to_int(object* aObject);
        double fn_to_float(object* aObject);

        object* int_from(object* aObject, long long aBase);

        object* b_int(std::vector<object*> const& aArgs, object* aKwargs)
        {
            object* base = nullptr;
            if (!keywords(aKwargs, { { "base", &base } }, "int") || !arity(aArgs, 0u, 2u, "int"))
                return nullptr;
            if (aArgs.size() == 2u)
                base = aArgs[1];
            if (aArgs.empty())
                return base != nullptr ? error("TypeError", "int() missing string argument") : make_int(0);
            long long b = -1;
            if (base != nullptr && !to_index(base, b))
                return nullptr;
            return int_from(aArgs[0], b);
        }

        object* b_float(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "float") || !arity(aArgs, 0u, 1u, "float"))
                return nullptr;
            if (aArgs.empty())
                return make_float(0.0);
            auto const value = fn_to_float(aArgs[0]);
            return failed() ? nullptr : make_float(value);
        }

        object* b_bool(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "bool") || !arity(aArgs, 0u, 1u, "bool"))
                return nullptr;
            if (aArgs.empty())
                return make_bool(false);
            auto const t = truth(aArgs[0]);
            return t < 0 ? nullptr : make_bool(t == 1);
        }

        object* b_complex(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "complex") || !arity(aArgs, 0u, 2u, "complex"))
                return nullptr;
            double re = 0.0, im = 0.0, re2 = 0.0, im2 = 0.0;
            if (!aArgs.empty() && kind_of(aArgs[0]) == NEOS_PY_STR)
            {
                // (CPython's complex_from_string)
                if (aArgs.size() > 1u)
                    return error("TypeError", "complex() can't take second arg if first is a string");
                std::string text = as<str_object>(aArgs[0])->value;
                auto const malformed = []() { return error("ValueError", "complex() arg is a malformed string"); };
                auto const trim = [](std::string& t)
                {
                    auto const first = t.find_first_not_of(" \t\n\r\f\v");
                    if (first == std::string::npos)
                    {
                        t.clear();
                        return;
                    }
                    t = t.substr(first, t.find_last_not_of(" \t\n\r\f\v") - first + 1u);
                };
                trim(text);
                if (text.size() >= 2u && text.front() == '(' && text.back() == ')')
                {
                    text = text.substr(1u, text.size() - 2u);
                    trim(text);
                }
                if (text.empty() || text.find_first_of("xX") != std::string::npos)
                    return malformed();
                char const* const begin = text.c_str();
                char const* p = begin;
                char const* const end = begin + text.size();
                auto const number = [&](double& aValue) -> bool
                {
                    char* stop = nullptr;
                    aValue = std::strtod(p, &stop);
                    if (stop == p)
                        return false;
                    p = stop;
                    return true;
                };
                double first = 0.0;
                if (number(first))
                {
                    if (p < end && (*p == 'j' || *p == 'J'))
                    {
                        im = first;
                        ++p;
                    }
                    else if (p < end && (*p == '+' || *p == '-'))
                    {
                        re = first;
                        double second = 0.0;
                        auto const sign = *p;
                        if (!number(second))
                        {
                            ++p;
                            second = sign == '-' ? -1.0 : 1.0;
                        }
                        if (p >= end || (*p != 'j' && *p != 'J'))
                            return malformed();
                        ++p;
                        im = second;
                    }
                    else
                        re = first;
                }
                else
                {
                    // (j, +j or -j)
                    double sign = 1.0;
                    if (p < end && (*p == '+' || *p == '-'))
                        sign = *p++ == '-' ? -1.0 : 1.0;
                    if (p >= end || (*p != 'j' && *p != 'J'))
                        return malformed();
                    ++p;
                    im = sign;
                }
                if (p != end)
                    return malformed();
                return make_complex(re, im);
            }
            auto const parts = [](object* aObject, double& aReal, double& aImag)
            {
                if (kind_of(aObject) == NEOS_PY_COMPLEX)
                {
                    aReal = as<complex_object>(aObject)->real;
                    aImag = as<complex_object>(aObject)->imag;
                    return true;
                }
                aImag = 0.0;
                return to_double(aObject, aReal);
            };
            if (!aArgs.empty() && !parts(aArgs[0], re, im))
                return nullptr;
            if (aArgs.size() > 1u && !parts(aArgs[1], re2, im2))
                return nullptr;
            // (as CPython: only complex parts are added (so that the signs of zeros are kept))
            if (aArgs.size() > 1u && kind_of(aArgs[1]) == NEOS_PY_COMPLEX)
                re -= im2;
            if (aArgs.size() > 1u && kind_of(aArgs[0]) == NEOS_PY_COMPLEX)
                re2 += im;
            return make_complex(re, aArgs.size() > 1u ? re2 : im);
        }

        object* b_abs(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "abs") || !arity(aArgs, 1u, 1u, "abs"))
                return nullptr;
            return unary(NEOS_PY_ABSOLUTE, aArgs[0]);
        }

        object* b_list(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "list") || !arity(aArgs, 0u, 1u, "list"))
                return nullptr;
            std::vector<object*> items;
            if (!aArgs.empty() && !collect(aArgs[0], items))
            {
                for (auto i : items)
                    release(i);
                return nullptr;
            }
            return make_list(std::move(items));
        }

        object* b_tuple(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "tuple") || !arity(aArgs, 0u, 1u, "tuple"))
                return nullptr;
            std::vector<object*> items;
            if (!aArgs.empty() && !collect(aArgs[0], items))
            {
                for (auto i : items)
                    release(i);
                return nullptr;
            }
            return make_tuple(std::move(items));
        }

        object* b_dict(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!arity(aArgs, 0u, 1u, "dict"))
                return nullptr;
            ref result{ make_dict() };
            if (!aArgs.empty() && !dict_update(result.get(), aArgs[0]))
                return nullptr;
            if (aKwargs != nullptr)
                for (auto const& item : as<dict_object>(aKwargs)->items)
                    if (dict_set(result.get(), item.first, item.second) < 0)
                        return nullptr;
            return result.release_ownership();
        }

        object* b_range(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "range"))
                return nullptr;
            if (aArgs.empty())
                return error("TypeError", "range expected at least 1 argument, got 0");
            if (aArgs.size() > 3u)
                return error("TypeError", "range expected at most 3 arguments, got " + std::to_string(aArgs.size()));
            long long values[3] = { 0, 0, 1 };
            for (std::size_t i = 0u; i < aArgs.size(); ++i)
                if (!to_index(aArgs[i], values[aArgs.size() == 1u ? 1u : i]))
                    return nullptr;
            if (values[2] == 0)
                return error("ValueError", "range() arg 3 must not be zero");
            return make_range(values[0], values[1], values[2]);
        }

        // (the items of an iterable)
        bool items_of(object* aIterable, std::vector<ref>& aItems)
        {
            std::vector<object*> items;
            bool const ok = collect(aIterable, items);
            for (auto i : items)
                aItems.emplace_back(i);
            return ok;
        }

        object* iterator_over(std::vector<object*> aItems)
        {
            ref const list{ make_list(std::move(aItems)) };
            return make_iterator(list.get());
        }

        // --- lazy iterators: map, filter, zip and enumerate (their values are computed as they are iterated)

        struct lazy_iterator_object
        {
            header h;
            std::vector<object*> iterators;     ///< (the iterators of the iterables)
            object* function = nullptr;         ///< (map's and filter's)
            long long count = 0;                ///< (enumerate's)
            bool strict = false;                ///< (zip's)
            bool exhausted = false;
            ~lazy_iterator_object() { for (auto i : iterators) release(i); release(function); }
        };

        object* lazy_next(neos_py_context*, object* aSelf);
        object* key_of(object* aKey, object* aItem);

        neos_py_type make_lazy_iterator_type(char const* aName)
        {
            auto type = make_type(aName, aName, retain_object, release_object<lazy_iterator_object>);
            type.iter = [](neos_py_context*, object* aSelf) -> object* { retain(aSelf); return aSelf; };
            type.next = lazy_next;
            return type;
        }

        neos_py_type sMapType = make_lazy_iterator_type("map");
        neos_py_type sFilterType = make_lazy_iterator_type("filter");
        neos_py_type sZipType = make_lazy_iterator_type("zip");
        neos_py_type sEnumerateType = make_lazy_iterator_type("enumerate");

        object* lazy_next(neos_py_context*, object* aSelf)
        {
            auto& it = *as<lazy_iterator_object>(aSelf);
            if (it.exhausted)
                return nullptr;
            auto const type = aSelf->type;
            if (type == &sFilterType.base)
            {
                for (;;)
                {
                    ref item{ next(it.iterators[0]) };
                    if (!item)
                    {
                        it.exhausted = !failed();
                        return nullptr;
                    }
                    ref const value{ key_of(it.function, item.get()) };
                    if (!value)
                        return nullptr;
                    auto const t = truth(value.get());
                    if (t < 0)
                        return nullptr;
                    if (t == 1)
                        return item.release_ownership();
                }
            }
            if (type == &sEnumerateType.base)
            {
                ref item{ next(it.iterators[0]) };
                if (!item)
                {
                    it.exhausted = !failed();
                    return nullptr;
                }
                return make_tuple({ make_int(it.count++), item.release_ownership() });
            }
            // (map and zip: an item of each iterable)
            std::vector<object*> items;
            for (std::size_t i = 0u; i < it.iterators.size(); ++i)
            {
                auto const item = next(it.iterators[i]);
                if (item == nullptr)
                {
                    for (auto x : items)
                        release(x);
                    if (failed())
                        return nullptr;
                    it.exhausted = true;
                    if (it.strict)
                    {
                        // (zip(strict=True): all of the iterables are exhausted together)
                        if (i > 0u)
                            return error("ValueError", "zip() argument " + std::to_string(i + 1u) + " is shorter than argument" + (i > 1u ? "s 1-" + std::to_string(i) : std::string{ " 1" }));
                        for (std::size_t j = 1u; j < it.iterators.size(); ++j)
                        {
                            ref const extra{ next(it.iterators[j]) };
                            if (extra)
                                return error("ValueError", "zip() argument " + std::to_string(j + 1u) + " is longer than argument" + (j > 1u ? "s 1-" + std::to_string(j) : std::string{ " 1" }));
                            if (failed())
                                return nullptr;
                        }
                    }
                    return nullptr;
                }
                items.push_back(item);
            }
            if (type == &sZipType.base)
                return make_tuple(std::move(items));
            ref const args{ make_tuple(std::move(items)) };
            return call(it.function, args.get(), nullptr);
        }

        object* lazy_iterator(neos_py_type& aType, std::vector<object*> const& aIterables, object* aFunction)
        {
            auto const it = new lazy_iterator_object{ { { &aType.base } } };
            ref const result{ &it->h.base };
            for (auto i : aIterables)
            {
                auto const iterator = iter(i);
                if (iterator == nullptr)
                    return nullptr;
                it->iterators.push_back(iterator);
            }
            retain(aFunction);
            it->function = aFunction;
            retain(result.get());
            return result.get();
        }

        object* b_enumerate(std::vector<object*> const& aArgs, object* aKwargs)
        {
            object* start = nullptr;
            if (!keywords(aKwargs, { { "start", &start } }, "enumerate") || !arity(aArgs, 1u, 2u, "enumerate"))
                return nullptr;
            if (aArgs.size() > 1u)
                start = aArgs[1];
            long long first = 0;
            if (start != nullptr && !to_index(start, first))
                return nullptr;
            auto const result = lazy_iterator(sEnumerateType, { aArgs[0] }, nullptr);
            if (result != nullptr)
                as<lazy_iterator_object>(result)->count = first;
            return result;
        }

        object* b_zip(std::vector<object*> const& aArgs, object* aKwargs)
        {
            object* strict = nullptr;
            if (!keywords(aKwargs, { { "strict", &strict } }, "zip"))
                return nullptr;
            auto const result = lazy_iterator(sZipType, aArgs, nullptr);
            if (result != nullptr && strict != nullptr)
            {
                auto const t = truth(strict);
                if (t < 0)
                {
                    release(result);
                    return nullptr;
                }
                as<lazy_iterator_object>(result)->strict = t == 1;
            }
            if (result != nullptr && aArgs.empty())
                as<lazy_iterator_object>(result)->exhausted = true;
            return result;
        }

        object* b_reversed(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "reversed") || !arity(aArgs, 1u, 1u, "reversed"))
                return nullptr;
            // (__reversed__: of a user class, or of a type of a module (e.g. collections.deque))
            if (is_instance(aArgs[0]))
            {
                if (ref const method{ special_method(aArgs[0], "__reversed__") })
                {
                    ref const empty{ make_tuple() };
                    return call(method.get(), empty.get(), nullptr);
                }
            }
            else if (kind_of(aArgs[0]) == NEOS_PY_OTHER && python_type(aArgs[0]) != nullptr && python_type(aArgs[0])->getattr != nullptr)
            {
                ref const method{ python_type(aArgs[0])->getattr(context(), aArgs[0], "__reversed__") };
                if (method)
                {
                    ref const empty{ make_tuple() };
                    return call(method.get(), empty.get(), nullptr);
                }
                clear_error();
            }
            auto const value = builtin_value(aArgs[0]);
            auto const k = kind_of(value);
            if (k != NEOS_PY_LIST && k != NEOS_PY_TUPLE && k != NEOS_PY_STR && k != NEOS_PY_RANGE && k != NEOS_PY_DICT && 
                !(k == NEOS_PY_OTHER && python_type(value) != nullptr && python_type(value)->getitem != nullptr))
                return error("TypeError", std::string{ "'" } + type_name(aArgs[0]) + "' object is not reversible");
            std::vector<ref> items;
            if (!items_of(value, items))
                return nullptr;
            std::vector<object*> result;
            for (auto i = items.rbegin(); i != items.rend(); ++i)
            {
                retain(i->get());
                result.push_back(i->get());
            }
            return iterator_over(std::move(result));
        }

        object* b_iter(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "iter") || !arity(aArgs, 1u, 2u, "iter"))
                return nullptr;
            if (aArgs.size() == 2u)
                return make_callable_iterator(aArgs[0], aArgs[1]);
            return iter(aArgs[0]);
        }

        object* b_next(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "next") || !arity(aArgs, 1u, 2u, "next"))
                return nullptr;
            auto const result = next(aArgs[0]);
            if (result != nullptr || failed())
                return result;
            if (aArgs.size() > 1u)
            {
                retain(aArgs[1]);
                return aArgs[1];
            }
            if (generator_stop(aArgs[0]))
                return nullptr;
            return error("StopIteration", "");
        }

        // a key function's value (aKey: null for the item itself)
        object* key_of(object* aKey, object* aItem)
        {
            if (aKey == nullptr || kind_of(aKey) == NEOS_PY_NONE)
            {
                retain(aItem);
                return aItem;
            }
            ref const args{ make_tuple({ (retain(aItem), aItem) }) };
            return call(aKey, args.get(), nullptr);
        }

        int less_than(object* a, object* b)
        {
            ref const less{ compare(NEOS_PY_LT, a, b) };
            if (!less)
                return -1;
            return truth(less.get());
        }

        object* minmax(std::vector<object*> const& aArgs, object* aKwargs, bool aMax)
        {
            char const* const name = aMax ? "max" : "min";
            object* key = nullptr;
            object* fallback = nullptr;
            if (!keywords(aKwargs, { { "key", &key }, { "default", &fallback } }, name))
                return nullptr;
            if (aArgs.empty())
                return error("TypeError", std::string{ name } + " expected at least 1 argument, got 0");
            std::vector<ref> items;
            if (aArgs.size() == 1u)
            {
                if (!items_of(aArgs[0], items))
                    return nullptr;
            }
            else
                for (auto a : aArgs)
                    items.push_back(ref::borrowed(a));
            if (items.empty())
            {
                if (fallback != nullptr)
                {
                    retain(fallback);
                    return fallback;
                }
                return error("ValueError", std::string{ name } + "() iterable argument is empty");
            }
            ref best = items[0];
            ref bestKey{ key_of(key, best.get()) };
            if (!bestKey)
                return nullptr;
            for (std::size_t i = 1u; i < items.size(); ++i)
            {
                ref const k{ key_of(key, items[i].get()) };
                if (!k)
                    return nullptr;
                auto const t = aMax ? less_than(bestKey.get(), k.get()) : less_than(k.get(), bestKey.get());
                if (t < 0)
                    return nullptr;
                if (t == 1)
                {
                    best = items[i];
                    bestKey = k;
                }
            }
            return best.release_ownership();
        }

        object* b_min(std::vector<object*> const& aArgs, object* aKwargs) { return minmax(aArgs, aKwargs, false); }
        object* b_max(std::vector<object*> const& aArgs, object* aKwargs) { return minmax(aArgs, aKwargs, true); }

        object* b_sum(std::vector<object*> const& aArgs, object* aKwargs)
        {
            object* start = nullptr;
            if (!keywords(aKwargs, { { "start", &start } }, "sum") || !arity(aArgs, 1u, 2u, "sum"))
                return nullptr;
            if (aArgs.size() > 1u)
                start = aArgs[1];
            if (start != nullptr && kind_of(start) == NEOS_PY_STR)
                return error("TypeError", "sum() can't sum strings [use ''.join(seq) instead]");
            // (a list's or tuple's small integers (e.g. sum([...]) of a generator expression's items: see the compiler's 
            // rewrite_comprehensions): summed in place (no code of the program runs, so nothing can change the list))
            if (auto const k = kind_of(aArgs[0]); (k == NEOS_PY_LIST || k == NEOS_PY_TUPLE) && (start == nullptr || (kind_of(start) == NEOS_PY_INT && !is_big(start))))
            {
                auto const& sequence = k == NEOS_PY_LIST ? as<list_object>(aArgs[0])->items : as<tuple_object>(aArgs[0])->items;
                long long integer = start != nullptr ? int_value(start) : 0;
                bool small = true;
                for (auto const item : sequence)
                    if (kind_of(item) != NEOS_PY_INT || is_big(item) || add_overflow(integer, int_value(item), &integer))
                    {
                        small = false;
                        break;
                    }
                if (small)
                    return make_int(integer);
            }
            std::vector<ref> items;
            if (!items_of(aArgs[0], items))
                return nullptr;
            ref total = start != nullptr ? ref::borrowed(start) : ref{ make_int(0) };
            // (CPython sums floats with compensated summation)
            bool allFloat = true;
            for (auto const& item : items)
                if (kind_of(item.get()) != NEOS_PY_FLOAT && kind_of(item.get()) != NEOS_PY_INT && kind_of(item.get()) != NEOS_PY_BOOL)
                    allFloat = false;
            if (allFloat && (kind_of(total.get()) == NEOS_PY_INT || kind_of(total.get()) == NEOS_PY_FLOAT || kind_of(total.get()) == NEOS_PY_BOOL))
            {
                std::size_t i = 0u;
                // integers first (exactly), then floats (Neumaier)
                long long integer = 0;
                std::optional<int128> wide;     // (the integers' sum beyond 64 bits, while it is a 128-bit integer)
                ref big;                        // (beyond 128 bits: an int)
                bool isFloat = kind_of(total.get()) == NEOS_PY_FLOAT;
                if (!isFloat)
                {
                    if (is_big(total.get()))
                        big = total;
                    else
                        integer = int_value(total.get());
                    for (; i < items.size() && kind_of(items[i].get()) != NEOS_PY_FLOAT; ++i)
                    {
                        auto const item = items[i].get();
                        if (!big && !wide && !is_big(item) && !add_overflow(integer, int_value(item), &integer))
                            continue;
                        if (!big)
                        {
                            int128 value, sum;
                            if (!wide)
                                wide = int128_of(integer);
                            if (int128_value(item, value) && add128(*wide, value, sum))
                            {
                                wide = sum;
                                continue;
                            }
                            big = ref{ make_int128(*wide) };
                        }
                        big = ref{ binary(NEOS_PY_ADD, big.get(), item, false) };
                        if (!big)
                            return nullptr;
                    }
                    if (i == items.size())
                        return big ? big.release_ownership() : wide ? make_int128(*wide) : make_int(integer);
                }
                double sum = isFloat ? as<float_object>(total.get())->value : static_cast<double>(integer);
                if (big && !to_double(big.get(), sum))
                    return nullptr;
                if (wide && !big)
                {
                    ref const partial{ make_int128(*wide) };
                    if (!to_double(partial.get(), sum))
                        return nullptr;
                }
                double c = 0.0;
                for (; i < items.size(); ++i)
                {
                    double x = 0.0;
                    to_double(items[i].get(), x);
                    double const t = sum + x;
                    if (std::fabs(sum) >= std::fabs(x))
                        c += (sum - t) + x;
                    else
                        c += (x - t) + sum;
                    sum = t;
                }
                // (as CPython: the compensation is not added if it is zero, or the sum is not finite)
                if (c != 0.0 && std::isfinite(c))
                    sum += c;
                return make_float(sum);
            }
            for (auto const& item : items)
            {
                ref next{ binary(NEOS_PY_ADD, total.get(), item.get(), false) };
                if (!next)
                    return nullptr;
                total = next;
            }
            return total.release_ownership();
        }

        object* any_all(std::vector<object*> const& aArgs, object* aKwargs, bool aAll)
        {
            if (!no_keywords(aKwargs, aAll ? "all" : "any") || !arity(aArgs, 1u, 1u, aAll ? "all" : "any"))
                return nullptr;
            // (the items until one decides it (as CPython's: the rest are not got))
            ref const it{ iter(aArgs[0]) };
            if (!it)
                return nullptr;
            for (;;)
            {
                ref const item{ next(it.get()) };
                if (!item)
                    return failed() ? nullptr : make_bool(aAll);
                auto const t = truth(item.get());
                if (t < 0)
                    return nullptr;
                if ((t == 1) != aAll)
                    return make_bool(!aAll);
            }
        }

        object* b_any(std::vector<object*> const& aArgs, object* aKwargs) { return any_all(aArgs, aKwargs, false); }
        object* b_all(std::vector<object*> const& aArgs, object* aKwargs) { return any_all(aArgs, aKwargs, true); }
        // (any and all of a generator expression (python::__any_items, python::__all_items: see the compiler's rewrite_comprehensions))
        object* fn_any_items(object* aIterable) { return checked(any_all({ aIterable }, nullptr, false)); }
        object* fn_all_items(object* aIterable) { return checked(any_all({ aIterable }, nullptr, true)); }

        object* b_sorted(std::vector<object*> const& aArgs, object* aKwargs)
        {
            object* key = nullptr;
            object* reverse = nullptr;
            if (!keywords(aKwargs, { { "key", &key }, { "reverse", &reverse } }, "sort") || !arity(aArgs, 1u, 1u, "sorted"))
                return nullptr;
            std::vector<ref> items;
            if (!items_of(aArgs[0], items))
                return nullptr;
            std::vector<std::pair<ref, ref>> keyed;
            for (auto const& item : items)
            {
                ref k{ key_of(key, item.get()) };
                if (!k)
                    return nullptr;
                keyed.emplace_back(k, item);
            }
            bool const descending = reverse != nullptr && truth(reverse) == 1;
            bool failure = false;
            // (stable: equal items keep their order, also when descending)
            std::stable_sort(keyed.begin(), keyed.end(), [&](auto const& a, auto const& b)
            {
                if (failure)
                    return false;
                auto const t = descending ? less_than(b.first.get(), a.first.get()) : less_than(a.first.get(), b.first.get());
                if (t < 0)
                    failure = true;
                return t == 1;
            });
            if (failure)
                return nullptr;
            std::vector<object*> result;
            for (auto& k : keyed)
                result.push_back(k.second.release_ownership());
            return make_list(std::move(result));
        }

        // --- classes (class statements: __class(name, bases, namespace)) and their instances

        struct class_object
        {
            header h;
            std::string name;
            std::vector<object*> bases;     ///< (classes; builtin bases (object, exception classes) are not kept)
            std::vector<object*> mro;       ///< (without the class itself)
            std::vector<std::pair<std::string, object*>> attributes;
            std::string exceptionBase;      ///< (the builtin exception class it derives from, if any)
            std::string module = "__main__";
            std::string builtinBase;        ///< (the builtin type it derives from (e.g. dict), if any: its instances have a value of it; "type": 
                                            ///< a metaclass, whose instances are classes)
            int (*instancecheck)(object*) = nullptr;    ///< (a class of a module (e.g. collections.abc.Iterable): its isinstance's test)
            object* metaclass = nullptr;    ///< (its metaclass (a class deriving from type), or null: type)
            std::string qualname;
            std::vector<object*> subclasses;    ///< (not owned: a subclass removes itself when it is destroyed)
            std::optional<std::vector<std::string>> slots;  ///< (its instances' attributes, if it and its bases define __slots__ (without __dict__))
            object* nativeBase = nullptr;   ///< (the type of a module it derives from (e.g. collections.deque), if any: its instances have a value of it)
            // (class_lookup's: names found (the class that has each, and its position in its attributes (null: none has it)), while no 
            // class's attributes or bases change (see sClassGeneration))
            struct lookup_hash { using is_transparent = void; std::size_t operator()(std::string_view aName) const { return std::hash<std::string_view>{}(aName); } };
            mutable std::unordered_map<std::string, std::pair<object*, std::size_t>, lookup_hash, std::equal_to<>> lookups;
            mutable std::uint64_t lookupsGeneration = 0u;
            // (class_hook's: its __getattribute__, __setattr__ and __delattr__ (class_lookup's), while no class changes)
            mutable object* hooks[3] = {};
            mutable std::uint64_t hooksGeneration = 0u;
            ~class_object();
        };

        struct instance_object
        {
            header h;
            object* cls;
            attribute_dict attributes;
            object* value = nullptr;        ///< (an instance of a subclass of a builtin type (e.g. dict): its value of that type)
            bool finalized = false;         ///< (its __del__ has been called)
            ~instance_object() { release(cls); release(value); }
        };

        struct bound_method_object { header h; object* function; object* self; ~bound_method_object() { release(function); release(self); } };
        struct wrapper_object { header h; int kind; object* function; object* setter; object* deleter = nullptr; object* doc = nullptr; ~wrapper_object() { release(function); release(setter); release(deleter); release(doc); } };   // (staticmethod, classmethod or property (doc: its __doc__, if given or set))
        struct super_object { header h; object* cls; object* self; ~super_object() { release(cls); release(self); } };

        enum : int { StaticMethod, ClassMethod, Property, CachedProperty };

        neos_py_type make_class_type();
        neos_py_type make_instance_type();
        neos_py_type make_bound_method_type();
        neos_py_type make_wrapper_type();
        neos_py_type make_super_type();
        neos_py_type sClassType = make_class_type();
        neos_py_type sInstanceType = make_instance_type();
        neos_py_type sBoundMethodType = make_bound_method_type();
        neos_py_type sWrapperType = make_wrapper_type();

        char const* wrapper_type_name(object* aObject)
        {
            if (aObject == nullptr || aObject->type != &sWrapperType.base)
                return nullptr;
            static char const* const sNames[] = { "staticmethod", "classmethod", "property", "functools.cached_property" };
            auto const kind = reinterpret_cast<wrapper_object*>(aObject)->kind;
            return kind >= 0 && kind <= 3 ? sNames[kind] : "property";
        }
        neos_py_type sSuperType = make_super_type();

        object* class_metaclass(object* aObject)
        {
            return is_class(aObject) ? as<class_object>(aObject)->metaclass : nullptr;
        }

        void classes_changed();

        class_object::~class_object()
        {
            // (class_lookup's cache (of classes by address) is stale)
            classes_changed();
            for (auto b : bases)
                if (b->type == &sClassType.base)
                    std::erase(reinterpret_cast<class_object*>(b)->subclasses, &h.base);
            for (auto b : bases) release(b);
            for (auto m : mro) release(m);
            for (auto& a : attributes) release(a.second);
            release(metaclass);
            release(nativeBase);
        }

        std::map<std::string, native_base_hooks, std::less<>>& native_bases()
        {
            static std::map<std::string, native_base_hooks, std::less<>> sHooks;
            return sHooks;
        }


        bool is_class(object* aObject) { return aObject != nullptr && aObject->type == &sClassType.base; }
        bool is_instance(object* aObject) { return aObject != nullptr && aObject->type == &sInstanceType.base; }

        std::string object_address(void const* aObject)
        {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%p", aObject);
            return std::string{ "0x" } + (std::string_view{ buffer }.starts_with("0x") ? buffer + 2 : buffer);
        }

        // (a user class's class (instances and exceptions of user exception classes): else null)
        object* user_class_of(object* aObject)
        {
            if (is_instance(aObject))
                return as<instance_object>(aObject)->cls;
            if (aObject != nullptr && aObject->type == &sExceptionType.base)
                return as<exception_object>(aObject)->cls;
            return nullptr;
        }

        // (an instance of a subclass of a builtin type: its value of that type (borrowed); else the object)
        object* builtin_value(object* aObject)
        {
            if (is_instance(aObject) && as<instance_object>(aObject)->value != nullptr)
                return as<instance_object>(aObject)->value;
            return aObject;
        }

        bool is_builtin_base(std::string_view aName)
        {
            return aName == "dict" || aName == "list" || aName == "tuple" || aName == "str" || aName == "int" || aName == "float" || aName == "set" ||
                aName == "frozenset" || aName == "bytes" || aName == "bytearray" || aName == "complex";
        }

        bool is_mutable_builtin(std::string_view aName)
        {
            return aName == "dict" || aName == "list" || aName == "set" || aName == "bytearray";
        }

        builtin_entry const* find_builtin(std::string_view aName);

        // (a value of a builtin type: the type called with arguments)
        object* builtin_construct(std::string const& aType, object* aArgs, object* aKwargs)
        {
            auto const* builtin = find_builtin(aType);
            if (builtin == nullptr)
                return error("TypeError", "cannot create '" + aType + "' instances");
            ref const empty{ make_tuple() };
            return call_builtin(context(), const_cast<builtin_entry*>(builtin), aArgs != nullptr ? aArgs : empty.get(), aKwargs);
        }

        // (an attribute of a class: in its MRO; borrowed; null if none)
        // (classes' attributes or bases changed: class_lookup's caches are stale)
        std::uint64_t sClassGeneration = 1u;
        void classes_changed()
        {
            ++sClassGeneration;
        }

        object* class_lookup_uncached(object* aClass, std::string_view aName, object** aOwner);

        namespace
        {
            // (class_lookup's cache of the names found recently (as CPython's type attribute cache): by the class's address and the 
            // name's size and first and last eight bytes (all of a name of up to 16 bytes; the rest of a longer one is checked too); its 
            // users hold the GIL (see threads.cpp))
            struct lookup_key
            {
                std::size_t size = 0u;
                std::uint64_t first = 0u;
                std::uint64_t last = 0u;
                explicit lookup_key(std::string_view aName) : size{ aName.size() }
                {
                    if (size >= 8u)
                    {
                        std::memcpy(&first, aName.data(), 8u);
                        std::memcpy(&last, aName.data() + size - 8u, 8u);
                    }
                    else
                        for (std::size_t i = 0u; i < size; ++i)
                            first |= static_cast<std::uint64_t>(static_cast<unsigned char>(aName[i])) << (8u * i);
                }
                lookup_key() = default;
                bool operator==(lookup_key const&) const = default;
                std::size_t hash(object const* aClass) const
                {
                    return static_cast<std::size_t>(((first * 0x9E3779B97F4A7C15ull) ^ (last * 0xC2B2AE3D27D4EB4Full) ^ (size << 7u) ^ 
                        ((reinterpret_cast<std::uintptr_t>(aClass) >> 4u) * 0x94D049BB133111EBull)) >> 32u);
                }
            };
            struct lookup_entry
            {
                object const* cls = nullptr;
                std::uint64_t generation = 0u;
                lookup_key key;
                object* owner = nullptr;
                std::size_t position = 0u;
                std::string middle;     ///< (a name of more than 16 bytes: its bytes between the first and last eight)
            };
            constexpr std::size_t LookupCacheSize = 1024u;
            lookup_entry sLookupCache[LookupCacheSize];
            std::string_view middle_of(std::string_view aName)
            {
                return aName.size() > 16u ? aName.substr(8u, aName.size() - 16u) : std::string_view{};
            }
        }

        object* class_lookup_cached(object* aClass, std::string_view aName, object** aOwner, lookup_key const& key, lookup_entry& entry);

        object* class_lookup(object* aClass, std::string_view aName, object** aOwner = nullptr)
        {
            if (aClass == nullptr)
                return nullptr;
            lookup_key const key{ aName };
            auto& entry = sLookupCache[key.hash(aClass) % LookupCacheSize];
            if (entry.cls == aClass && entry.generation == sClassGeneration && entry.key == key && (key.size <= 16u || entry.middle == middle_of(aName))) [[likely]]
            {
                if (entry.owner == nullptr)
                    return nullptr;
                if (aOwner != nullptr)
                    *aOwner = entry.owner;
                return as<class_object>(entry.owner)->attributes[entry.position].second;
            }
            return class_lookup_cached(aClass, aName, aOwner, key, entry);
        }

        // (not in the cache of recent names: the class's own)
#if defined(_MSC_VER)
        __declspec(noinline)
#else
        __attribute__((noinline))
#endif
        object* class_lookup_cached(object* aClass, std::string_view aName, object** aOwner, lookup_key const& key, lookup_entry& entry)
        {
            // (found before: the class that has it (and where), or none)
            auto const& cls = *as<class_object>(aClass);
            auto const remember = [&](object* aFound, std::size_t aPosition)
            {
                entry.cls = aClass;
                entry.generation = sClassGeneration;
                entry.key = key;
                entry.owner = aFound;
                entry.position = aPosition;
                entry.middle.assign(middle_of(aName));
            };
            if (cls.lookupsGeneration != sClassGeneration)
            {
                cls.lookups.clear();
                cls.lookupsGeneration = sClassGeneration;
            }
            if (auto const found = cls.lookups.find(aName); found != cls.lookups.end())
            {
                auto const [owner, position] = found->second;
                remember(owner, position);
                if (owner == nullptr)
                    return nullptr;
                if (aOwner != nullptr)
                    *aOwner = owner;
                return as<class_object>(owner)->attributes[position].second;
            }
            object* owner = nullptr;
            auto const result = class_lookup_uncached(aClass, aName, &owner);
            std::size_t position = 0u;
            if (result != nullptr)
            {
                // (its position: by its name (attributes can have the same value, e.g. a small int))
                auto const& attributes = as<class_object>(owner)->attributes;
                while (position < attributes.size() && attributes[position].first != aName)
                    ++position;
                if (aOwner != nullptr)
                    *aOwner = owner;
            }
            if (result == nullptr || position < as<class_object>(owner)->attributes.size())
                cls.lookups.emplace(std::string{ aName }, std::make_pair(result != nullptr ? owner : nullptr, position));
            return result;
        }

        enum class_hook_name { GetAttributeHook, SetAttrHook, DelAttrHook };

        // (a class's __getattribute__, __setattr__ or __delattr__ (as class_lookup finds it; null: none): found once while no class changes)
        object* class_hook(object* aClass, class_hook_name aHook)
        {
            static char const* const sNames[] = { "__getattribute__", "__setattr__", "__delattr__" };
            if (aClass == nullptr)
                return nullptr;
            auto const& cls = *as<class_object>(aClass);
            if (cls.hooksGeneration != sClassGeneration)
            {
                for (std::size_t i = 0u; i < 3u; ++i)
                    cls.hooks[i] = class_lookup(aClass, sNames[i]);
                cls.hooksGeneration = sClassGeneration;
            }
            return cls.hooks[aHook];
        }

        object* class_lookup_uncached(object* aClass, std::string_view aName, object** aOwner)
        {
            auto const find = [&](object* c) -> object*
            {
                for (auto& a : as<class_object>(c)->attributes)
                    if (a.first == aName)
                    {
                        if (aOwner != nullptr)
                            *aOwner = c;
                        return a.second;
                    }
                return nullptr;
            };
            if (auto const r = find(aClass))
                return r;
            for (auto c : as<class_object>(aClass)->mro)
                if (auto const r = find(c))
                    return r;
            return nullptr;
        }

        bool is_subclass(object* aClass, object* aBase)
        {
            if (aClass == aBase)
                return true;
            for (auto c : as<class_object>(aClass)->mro)
                if (c == aBase)
                    return true;
            return false;
        }

        object* make_bound_method(object* aFunction, object* aSelf)
        {
            retain(aFunction);
            retain(aSelf);
            return &(new bound_method_object{ { { &sBoundMethodType.base } }, aFunction, aSelf })->h.base;
        }

        std::vector<object*> class_subclasses(object* aClass)
        {
            return is_class(aClass) ? as<class_object>(aClass)->subclasses : std::vector<object*>{};
        }

        std::set<object*>& abstract_builtins()
        {
            static std::set<object*> sFunctions;
            return sFunctions;
        }

        std::set<neos_rt_object_type const*>& method_types()
        {
            static std::set<neos_rt_object_type const*> sTypes;
            return sTypes;
        }

        // (an attribute found in a class, as accessed through an instance (or the class itself: aSelf null))
        object* bind_attribute(object* aAttribute, object* aSelf, object* aClass)
        {
            // (a descriptor (an instance of a class with __get__): its __get__(instance (or None), class))
            if (is_instance(aAttribute))
                if (auto const cls = as<instance_object>(aAttribute)->cls; class_lookup(cls, "__get__") != nullptr)
                {
                    ref const get{ bind_attribute(class_lookup(cls, "__get__"), aAttribute, cls) };
                    ref const args{ make_tuple({ aSelf != nullptr ? (retain(aSelf), aSelf) : (retain(none()), none()), (retain(aClass), aClass) }) };
                    return call(get.get(), args.get(), nullptr);
                }
            if (aAttribute->type == &sWrapperType.base)
            {
                auto const& w = *as<wrapper_object>(aAttribute);
                switch (w.kind)
                {
                case StaticMethod:
                    retain(w.function);
                    return w.function;
                case ClassMethod:
                    return make_bound_method(w.function, aClass);
                default:
                    if (aSelf == nullptr)
                    {
                        retain(aAttribute);
                        return aAttribute;
                    }
                    {
                        retain(aSelf);
                        ref const args{ make_tuple({ aSelf }) };
                        ref value{ call(w.function, args.get(), nullptr) };
                        // (functools.cached_property: the value becomes the instance's attribute)
                        if (value && w.kind == CachedProperty)
                        {
                            ref const name{ getattr(w.function, "__name__") };
                            if (name && kind_of(name.get()) == NEOS_PY_STR && setattr(aSelf, as<str_object>(name.get())->value.c_str(), value.get()) < 0)
                                return nullptr;
                            clear_error();
                        }
                        return value.release_ownership();
                    }
                }
            }
            if (aSelf != nullptr && (aAttribute->type == &sScriptFunctionType.base || (kind_of(aAttribute) == NEOS_PY_FUNCTION && as<function_object>(aAttribute)->self == nullptr &&
                as<function_object>(aAttribute)->binds) ||
                method_types().contains(aAttribute->type)))
                return make_bound_method(aAttribute, aSelf);
            retain(aAttribute);
            return aAttribute;
        }

        // (a special method of an object's user class, bound to it: null (no exception) if it has none)
        object* special_method(object* aObject, char const* aName)
        {
            auto const cls = user_class_of(aObject);
            if (cls == nullptr)
                return nullptr;
            auto const attribute = class_lookup(cls, aName);
            if (attribute == nullptr)
                return nullptr;
            return bind_attribute(attribute, aObject, cls);
        }

        bool has_special_method(object* aObject, char const* aName)
        {
            auto const cls = user_class_of(aObject);
            return cls != nullptr && class_lookup(cls, aName) != nullptr;
        }

        // (a call of a special method: aFound false if there is none)
        object* call_special(object* aObject, char const* aName, std::vector<object*> aArgs, bool& aFound) // (steals the arguments)
        {
            ref const method{ special_method(aObject, aName) };
            aFound = static_cast<bool>(method);
            if (!method)
            {
                for (auto a : aArgs)
                    release(a);
                return nullptr;
            }
            ref const args{ make_tuple(std::move(aArgs)) };
            return call(method.get(), args.get(), nullptr);
        }

        object* instance_getattr(neos_py_context*, object* aSelf, char const* aName);

        object* attributes_dict(std::vector<std::pair<std::string, object*>> const& aAttributes)
        {
            auto const d = make_dict();
            for (auto const& a : aAttributes)
            {
                ref const key{ make_str(a.first) };
                dict_set(d, key.get(), a.second);
            }
            return d;
        }

        // (a data descriptor: a property, or an instance of a class with __set__ or __delete__)
        bool data_descriptor(object* aAttribute)
        {
            if (aAttribute->type == &sWrapperType.base)
                return as<wrapper_object>(aAttribute)->kind == Property;
            if (is_instance(aAttribute))
            {
                auto const cls = as<instance_object>(aAttribute)->cls;
                return class_lookup(cls, "__set__") != nullptr || class_lookup(cls, "__delete__") != nullptr;
            }
            return false;
        }

        object* user_getattr_default(object* aSelf, attribute_dict& aAttributes, char const* aName);
        object* object_attribute(object* aSelf, std::string_view aName);

        // (getattr of an instance, or of an exception of a user exception class: its class's __getattribute__ (and then its 
        // __getattr__), if it has one)
        // (a name that a cache's entry is for: its text's address (a call's) and its text (as it was then: an address can be another 
        // text's later))
        struct name_text
        {
            char const* name = nullptr;
            std::uint8_t length = 0u;
            char text[23] = {};
            bool matches(char const* aName) const { return name == aName && std::strcmp(text, aName) == 0; }
            std::string_view view() const { return std::string_view{ text, length }; }
            bool set(char const* aName)
            {
                auto const size = std::strlen(aName);
                if (size >= sizeof(text))
                    return false;
                std::memcpy(text, aName, size + 1u);
                length = static_cast<std::uint8_t>(size);
                name = aName;
                return true;
            }
        };

        // (instances' attributes read and set (by getattr and setattr) by name and class: where each is in its instance's attributes, 
        // while no class changes, so that it is not looked up again (CPython's inline caches))
        struct attribute_cache_entry
        {
            object const* cls = nullptr;
            char const* name = nullptr;
            std::uint64_t generation = 0u;
            std::size_t position = 0u;
        };

        // (an attribute's key is a name's (a NUL terminated text's): with no length found for the text)
        inline bool key_named(object* aKey, char const* aName)
        {
            if (aKey->type != &sStrType.base)
                return false;
            auto const& key = as<str_object>(aKey)->value;
            auto const* const text = key.data();
            auto const length = key.size();
            for (std::size_t i = 0u; i < length; ++i)
                if (text[i] != aName[i] || aName[i] == '\0')
                    return false;
            return aName[length] == '\0';
        }
        constexpr std::size_t AttributeCacheSize = 512u;
        attribute_cache_entry sReadCache[AttributeCacheSize];
        attribute_cache_entry sWriteCache[AttributeCacheSize];

        attribute_cache_entry& attribute_cache(attribute_cache_entry* aCache, object const* aClass, char const* aName)
        {
            auto const h = ((reinterpret_cast<std::uintptr_t>(aClass) >> 4u) * 0x9E3779B97F4A7C15ull) ^ (reinterpret_cast<std::uintptr_t>(aName) * 0xC2B2AE3D27D4EB4Full);
            return aCache[(h >> 40u) % AttributeCacheSize];
        }

        // (the item of an instance's attributes that a cache entry is for: null if it is not for it (or no longer))
        std::pair<object*, object*>* cached_item(attribute_cache_entry const& aEntry, instance_object& aInstance, char const* aName)
        {
            if (aEntry.cls != aInstance.cls || aEntry.name != aName || aEntry.generation != sClassGeneration || aInstance.attributes.dict == nullptr)
                return nullptr;
            auto& items = as<dict_object>(aInstance.attributes.dict)->items;
            if (aEntry.position >= items.size() || !key_named(items[aEntry.position].first, aName))
                return nullptr;
            return &items[aEntry.position];
        }

        void remember_item(attribute_cache_entry* aCache, instance_object& aInstance, char const* aName)
        {
            if (aInstance.attributes.dict == nullptr)
                return;
            auto const& items = as<dict_object>(aInstance.attributes.dict)->items;
            std::string_view const name{ aName };
            for (std::size_t i = 0u; i < items.size(); ++i)
                if (attribute_name(items[i].first, name))
                {
                    attribute_cache(aCache, aInstance.cls, aName) = { aInstance.cls, aName, sClassGeneration, i };
                    return;
                }
        }

        object* instance_attribute_cached(object* aObject, char const* aName)
        {
            auto& instance = *as<instance_object>(aObject);
            if (auto const item = cached_item(attribute_cache(sReadCache, instance.cls, aName), instance, aName))
                return (retain(item->second), item->second);
            return nullptr;
        }

        bool instance_set_cached(object* aObject, char const* aName, object* aValue)
        {
            auto& instance = *as<instance_object>(aObject);
            if (auto const item = cached_item(attribute_cache(sWriteCache, instance.cls, aName), instance, aName))
            {
                retain(aValue);
                release(item->second);
                item->second = aValue;
                return true;
            }
            return false;
        }

        object* user_getattr(object* aSelf, attribute_dict& aAttributes, char const* aName)
        {
            auto const cls = user_class_of(aSelf);
            if (cls != nullptr && std::strcmp(aName, "__class__") != 0)
                if (auto const hook = class_hook(cls, GetAttributeHook))
                {
                    ref const method{ bind_attribute(hook, aSelf, cls) };
                    ref const args{ make_tuple({ make_str(aName) }) };
                    ref result{ call(method.get(), args.get(), nullptr) };
                    if (!result && failed() && std::string_view{ error_text() }.starts_with("AttributeError"))
                        if (auto const getattrHook = class_lookup(cls, "__getattr__"))
                        {
                            clear_error();
                            ref const fallback{ bind_attribute(getattrHook, aSelf, cls) };
                            return call(fallback.get(), args.get(), nullptr);
                        }
                    return result.release_ownership();
                }
            return user_getattr_default(aSelf, aAttributes, aName);
        }

        // (object.__getattribute__)
        object* user_getattr_default(object* aSelf, attribute_dict& aAttributes, char const* aName)
        {
            std::string_view const name{ aName };
            auto const cls = user_class_of(aSelf);
            if (name == "__class__")
            {
                retain(cls);
                return cls;
            }
            if (name == "__dict__")
            {
                // (__slots__ without __dict__: none)
                if (cls != nullptr && is_class(cls) && as<class_object>(cls)->slots && 
                    std::find(as<class_object>(cls)->slots->begin(), as<class_object>(cls)->slots->end(), "__dict__") == as<class_object>(cls)->slots->end())
                    return nullptr;
                auto const d = aAttributes.get_dict();
                return (retain(d), d);
            }
            // (a data descriptor (e.g. a property) of the class first, then the instance's attributes, then the class's)
            auto const attribute = class_lookup(cls, name);
            if (attribute != nullptr && data_descriptor(attribute))
                return bind_attribute(attribute, aSelf, cls);
            if (auto const a = aAttributes.find(name))
            {
                // (an instance's own attribute (with no __getattribute__): where it is, remembered)
                if (is_instance(aSelf) && &aAttributes == &as<instance_object>(aSelf)->attributes && class_hook(cls, GetAttributeHook) == nullptr)
                    remember_item(sReadCache, *as<instance_object>(aSelf), aName);
                return (retain(a), a);
            }
            if (attribute != nullptr)
                return bind_attribute(attribute, aSelf, cls);
            if (auto const value = builtin_value(aSelf); value != aSelf)
            {
                // (the builtin type's methods: of the value)
                if (auto const method = protocol_method(value, kind_of(value), aName))
                    return method;
                if (ref r{ getattr(value, aName) })
                    return r.release_ownership();
                clear_error();
            }
            // (object's attributes)
            if (auto const a = object_attribute(aSelf, name))
                return a;
            if (name == "__getattribute__" || name == "__setattr__" || name == "__delattr__" || name == "__init__" || name == "__new__" ||
                name == "__init_subclass__")
            {
                ref const unbound{ class_function("object", aName) };
                if (unbound && kind_of(unbound.get()) == NEOS_PY_FUNCTION && name != "__new__" && name != "__init_subclass__")
                    return make_function(aName, as<function_object>(unbound.get())->function, as<function_object>(unbound.get())->data, aSelf);
                if (unbound)
                    return new_reference(unbound.get());
            }
            if (auto const getattr = class_lookup(cls, "__getattr__"))
            {
                ref const method{ bind_attribute(getattr, aSelf, cls) };
                ref const args{ make_tuple({ make_str(std::string{ name }) }) };
                return call(method.get(), args.get(), nullptr);
            }
            return nullptr;
        }

        int user_setattr_default(object* aSelf, attribute_dict& aAttributes, char const* aName, object* aValue);

        // (setattr (aValue: null: delattr) of an instance, or of an exception of a user exception class: its class's __setattr__
        // (__delattr__), if it has one)
        int user_setattr(object* aSelf, attribute_dict& aAttributes, char const* aName, object* aValue)
        {
            auto const cls = user_class_of(aSelf);
            if (cls != nullptr)
                if (auto const hook = class_hook(cls, aValue != nullptr ? SetAttrHook : DelAttrHook))
                {
                    ref const method{ bind_attribute(hook, aSelf, cls) };
                    ref const args{ aValue != nullptr ? make_tuple({ make_str(aName), (retain(aValue), aValue) }) : make_tuple({ make_str(aName) }) };
                    ref const r{ call(method.get(), args.get(), nullptr) };
                    return r ? 0 : -1;
                }
            return user_setattr_default(aSelf, aAttributes, aName, aValue);
        }

        // (object.__setattr__ and object.__delattr__)
        int user_setattr_default(object* aSelf, attribute_dict& aAttributes, char const* aName, object* aValue)
        {
            auto const cls = user_class_of(aSelf);
            auto const classAttribute = class_lookup(cls, aName);
            if (auto const attribute = classAttribute; attribute != nullptr && attribute->type == &sWrapperType.base &&
                as<wrapper_object>(attribute)->kind == Property)
            {
                auto const& p = *as<wrapper_object>(attribute);
                auto const function = aValue != nullptr ? p.setter : p.deleter;
                if (function == nullptr)
                {
                    error("AttributeError", "property '" + std::string{ aName } + "' of '" + as<class_object>(cls)->name + "' object has no " + (aValue != nullptr ? "setter" : "deleter"));
                    return -1;
                }
                retain(aSelf);
                ref const args{ aValue != nullptr ? make_tuple({ aSelf, (retain(aValue), aValue) }) : make_tuple({ aSelf }) };
                ref const r{ call(function, args.get(), nullptr) };
                return r ? 0 : -1;
            }
            // (a descriptor's __set__ or __delete__)
            if (auto const attribute = classAttribute; attribute != nullptr && is_instance(attribute))
            {
                auto const descriptorClass = as<instance_object>(attribute)->cls;
                if (auto const function = class_lookup(descriptorClass, aValue != nullptr ? "__set__" : "__delete__"))
                {
                    ref const method{ bind_attribute(function, attribute, descriptorClass) };
                    ref const args{ aValue != nullptr ? make_tuple({ (retain(aSelf), aSelf), (retain(aValue), aValue) }) : make_tuple({ (retain(aSelf), aSelf) }) };
                    ref const r{ call(method.get(), args.get(), nullptr) };
                    return r ? 0 : -1;
                }
            }
            // (__slots__: the only attributes it can have)
            if (cls != nullptr && as<class_object>(cls)->slots && aValue != nullptr)
            {
                auto const& slots = *as<class_object>(cls)->slots;
                if (std::find(slots.begin(), slots.end(), aName) == slots.end())
                {
                    error("AttributeError", "'" + as<class_object>(cls)->name + "' object has no attribute '" + aName + "' and no __dict__ for setting new attributes");
                    return -1;
                }
            }
            if (aValue == nullptr)
            {
                // (del object.name)
                if (aAttributes.erase(aName))
                    return 0;
                error("AttributeError", std::string{ cls != nullptr && as<class_object>(cls)->slots ? "" : "'" + std::string{ type_name(aSelf) } + "' object has no attribute '" } + aName +
                    (cls != nullptr && as<class_object>(cls)->slots ? "" : "'"));
                return -1;
            }
            aAttributes.set(aName, aValue);
            // (an instance's own attribute (its class has none of the name, and no __setattr__): where it is, remembered)
            if (is_instance(aSelf) && &aAttributes == &as<instance_object>(aSelf)->attributes && classAttribute == nullptr && class_hook(cls, SetAttrHook) == nullptr)
                remember_item(sWriteCache, *as<instance_object>(aSelf), aName);
            return 0;
        }

        // --- instances

        object* instance_repr(neos_py_context*, object* aSelf)
        {
            bool found = false;
            if (auto const r = call_special(aSelf, "__repr__", {}, found); found)
                return r;
            if (auto const value = builtin_value(aSelf); value != aSelf)
            {
                std::string text;
                return repr_of(value, text) ? make_str(text) : nullptr;
            }
            auto const cls = as<instance_object>(aSelf)->cls;
            return make_str("<" + as<class_object>(cls)->module + "." + as<class_object>(cls)->name + " object at " + object_address(aSelf) + ">");
        }

        object* instance_str(neos_py_context* aContext, object* aSelf)
        {
            bool found = false;
            if (auto const r = call_special(aSelf, "__str__", {}, found); found)
                return r;
            if (kind_of(builtin_value(aSelf)) == NEOS_PY_STR)
            {
                retain(builtin_value(aSelf));
                return builtin_value(aSelf);
            }
            return instance_repr(aContext, aSelf);
        }

        object* instance_getattr(neos_py_context*, object* aSelf, char const* aName)
        {
            auto& instance = *as<instance_object>(aSelf);
            if (auto const r = user_getattr(aSelf, instance.attributes, aName))
                return r;
            if (failed())
                return nullptr;
            return error("AttributeError", "'" + as<class_object>(instance.cls)->name + "' object has no attribute '" + aName + "'");
        }

        // (object.name(args): a method of a builtin type's object, or a function of an instance's class, called without a bound method made 
        // for it (as CPython's LOAD_ATTR of a method): python::load_method (the method is found: a pending_method_object, which call calls; 
        // otherwise the attribute, as getattr's), then python::call or python::call_arguments)
        struct method_target
        {
            native_method function = nullptr;   ///< (a builtin type's method)
            bool keywords = true;               ///< (its keyword arguments)
            object* classFunction = nullptr;    ///< (an instance's class's function (borrowed))
        };

        bool find_method(object* aObject, char const* aName, method_target& aTarget)
        {
            auto const k = kind_of(aObject);
            if (k != NEOS_PY_OTHER && !(k == NEOS_PY_ITERATOR && aName[0] == '_' && aName[1] == '_'))
            {
                // (a builtin type's method found before for the name (the methods do not change))
                struct builtin_cache_entry
                {
                    int kind = NEOS_PY_OTHER;
                    name_text name;
                    method_target target;
                };
                static builtin_cache_entry sBuiltinCache[128];
                auto& cached = sBuiltinCache[((reinterpret_cast<std::uintptr_t>(aName) * 0x9E3779B97F4A7C15ull ^ static_cast<std::uintptr_t>(k) * 0xC2B2AE3D27D4EB4Full) >> 40u) % 128u];
                if (cached.kind == k && cached.name.matches(aName))
                {
                    aTarget = cached.target;
                    return true;
                }
                auto const found = [&](method_target const& aFound)
                {
                    aTarget = aFound;
                    if (cached.name.set(aName))
                    {
                        cached.kind = k;
                        cached.target = aFound;
                    }
                    else
                        cached.kind = NEOS_PY_OTHER;
                    return true;
                };
                // (as getattr finds them: sMethods, then str's and the containers' methods)
                for (auto const& method : sMethods)
                    if (method.kind == k && std::strcmp(method.name, aName) == 0)
                        return found({ method.data.function, method.data.function == str_format || method.data.function == int_to_bytes, nullptr });
                if (k == NEOS_PY_STR)
                    if (auto const f = str_method_function(aName))
                        return found({ f, true, nullptr });
                if (k == NEOS_PY_LIST || k == NEOS_PY_TUPLE || k == NEOS_PY_DICT)
                    if (auto const f = container_method_function(k, aName))
                        return found({ f, true, nullptr });
                return false;
            }
            if (is_instance(aObject))
            {
                // (its class's function for the name, found before (while no class changes))
                struct method_cache_entry
                {
                    object const* cls = nullptr;
                    name_text name;
                    std::uint64_t generation = 0u;
                    object* function = nullptr;
                };
                static method_cache_entry sMethodCache[256];
                auto const cls = user_class_of(aObject);
                auto& cached = sMethodCache[(((reinterpret_cast<std::uintptr_t>(cls) >> 4u) * 0x9E3779B97F4A7C15ull ^ reinterpret_cast<std::uintptr_t>(aName) * 0xC2B2AE3D27D4EB4Full) >> 40u) % 256u];
                if (cached.cls == cls && cached.generation == sClassGeneration && cached.name.matches(aName))
                {
                    if (as<instance_object>(aObject)->attributes.find(cached.name.view()) != nullptr)
                        return false;
                    aTarget = { nullptr, true, cached.function };
                    return true;
                }
                if (std::strcmp(aName, "__class__") == 0 || std::strcmp(aName, "__dict__") == 0)
                    return false;
                // (a function of its class (not a data descriptor) that its attributes do not hide, and no __getattribute__ (as user_getattr))
                if (cls != nullptr && class_hook(cls, GetAttributeHook) == nullptr)
                    if (auto const attribute = class_lookup(cls, aName); attribute != nullptr && 
                        (attribute->type == &sScriptFunctionType.base || (kind_of(attribute) == NEOS_PY_FUNCTION && as<function_object>(attribute)->self == nullptr && 
                            as<function_object>(attribute)->binds)) && as<instance_object>(aObject)->attributes.find(aName) == nullptr)
                    {
                        // (the class's attribute is borrowed: it is its class's while no class changes)
                        if (cached.name.set(aName))
                        {
                            cached.cls = cls;
                            cached.generation = sClassGeneration;
                            cached.function = attribute;
                        }
                        else
                            cached.cls = nullptr;
                        aTarget = { nullptr, true, attribute };
                        return true;
                    }
            }
            return false;
        }

        // (load_method's method (a builtin type's or a class's): called by call (python::call or python::call_arguments) as the method of 
        // its object (it is not otherwise seen))
        struct pending_method_object
        {
            header h;
            object* self;
            char const* name;
            method_target target;
            ~pending_method_object() { release(self); release(target.classFunction); }
            static void* operator new(std::size_t aSize);
            static void operator delete(void* aObject, std::size_t aSize) noexcept;
        };
        namespace
        {
            object_pool<sizeof(pending_method_object)> sPendingMethodPool;
        }
        void* pending_method_object::operator new(std::size_t aSize)
        {
            return aSize == sizeof(pending_method_object) ? sPendingMethodPool.allocate() : ::operator new(aSize);
        }
        void pending_method_object::operator delete(void* aObject, std::size_t aSize) noexcept
        {
            if (aSize == sizeof(pending_method_object))
                sPendingMethodPool.deallocate(aObject);
            else
                ::operator delete(aObject);
        }
        neos_py_type sPendingMethodType = make_type("python.pending_method", "pending_method", retain_object, release_object<pending_method_object>);

        object* load_method(object* aObject, char const* aName)
        {
            if (method_target target; find_method(aObject, aName, target))
            {
                retain(aObject);
                retain(target.classFunction);
                return &(new pending_method_object{ { { &sPendingMethodType.base } }, aObject, aName, target })->h.base;
            }
            auto const result = getattr(aObject, aName);
            if (result == nullptr)
                note_attribute_error(aObject);
            return result;
        }

        bool is_pending_method(object* aObject)
        {
            return aObject != nullptr && aObject->type == &sPendingMethodType.base;
        }

        // (a call's arguments as a tuple (aArgs: the call's, if it has one))
        ref arguments_tuple(std::span<object* const> aItems, object* aArgs)
        {
            if (aArgs != nullptr)
                return ref::borrowed(aArgs);
            std::vector<object*> items{ aItems.begin(), aItems.end() };
            for (auto a : items)
                retain(a);
            return ref{ make_tuple(std::move(items)) };
        }

        // (a call's arguments as a native method takes them (a vector): while no thread has been started, one of the vectors kept for 
        // calls (a call that is made while one is made has the next), so that a call allocates none)
        struct pooled_arguments
        {
            static constexpr std::size_t PoolSize = 32u;
            static inline std::vector<object*> sPool[PoolSize];
            static inline std::size_t sDepth = 0u;
            std::optional<std::vector<object*>> own;
            std::vector<object*>& items;
            explicit pooled_arguments(std::span<object* const> aItems) : 
                items{ sDepth < PoolSize && !threads_started() ? sPool[sDepth] : own.emplace() }
            {
                if (!own)
                    ++sDepth;
                items.assign(aItems.begin(), aItems.end());
            }
            ~pooled_arguments()
            {
                if (!own)
                {
                    items.clear();
                    --sDepth;
                }
            }
            pooled_arguments(pooled_arguments const&) = delete;
            pooled_arguments& operator=(pooled_arguments const&) = delete;
        };

        // (a call of load_method's method: its arguments aItems (aArgs: them as a tuple, if the call has one))
        object* pending_method_call(object* aPending, std::span<object* const> aItems, object* aArgs, object* aKwargs)
        {
            ref const keep = ref::borrowed(aPending);
            auto const& m = *reinterpret_cast<pending_method_object*>(aPending);
            auto const& target = m.target;
            // (a function of the program: its instance first, with no tuple made for it)
            if (target.classFunction != nullptr && target.classFunction->type == &sScriptFunctionType.base)
                return script_function_invoke(target.classFunction, m.self, aItems, aKwargs);
            if (target.function != nullptr)
            {
                bool const anyKeyword = aKwargs != nullptr && as<dict_object>(aKwargs)->items.size() != 0u;
                if (anyKeyword && !target.keywords)
                    return error("TypeError", std::string{ m.name } + "() takes no keyword arguments");
                if (aArgs != nullptr)
                    return target.function(m.self, tuple_items(aArgs), anyKeyword ? aKwargs : nullptr);
                pooled_arguments const items{ aItems };
                return target.function(m.self, items.items, anyKeyword ? aKwargs : nullptr);
            }
            std::vector<object*> items;
            items.reserve(aItems.size() + 1u);
            items.push_back((retain(m.self), m.self));
            for (auto a : aItems)
                items.push_back((retain(a), a));
            ref const bound{ make_tuple(std::move(items)) };
            return call(target.classFunction, bound.get(), aKwargs);
        }

        // (a call of an object with its arguments aItems (aArgs: them as a tuple, if the call has one): load_method's method, a function of 
        // the program, or a bound method of one, is called with no tuple made for them)
        object* arguments_call(object* aCallable, std::span<object* const> aItems, object* aArgs, object* aKwargs)
        {
            if (aCallable != nullptr && aCallable->type == &sPendingMethodType.base)
                return pending_method_call(aCallable, aItems, aArgs, aKwargs);
            if (aCallable != nullptr && aCallable->type == &sScriptFunctionType.base)
                return script_function_invoke(aCallable, nullptr, aItems, aKwargs);
            if (aCallable != nullptr && aCallable->type == &sBoundMethodType.base && as<bound_method_object>(aCallable)->function->type == &sScriptFunctionType.base)
            {
                ref const method = ref::borrowed(aCallable);
                return script_function_invoke(as<bound_method_object>(aCallable)->function, as<bound_method_object>(aCallable)->self, aItems, aKwargs);
            }
            auto const args = arguments_tuple(aItems, aArgs);
            return call(aCallable, args.get(), aKwargs);
        }

        // (the number of arguments that the program calls a function of the program with itself (see python::direct_function): -1 if it 
        // does not)
        std::int32_t direct_parameters(script_function_object const& f)
        {
            if (f.directParameters == -2)
                f.directParameters = f.program != nullptr || f.flavour != 0 || !f.cells.empty() || (f.result != 'O' && f.result != 'V') ||
                    !std::all_of(f.parameters.begin(), f.parameters.end(), [](script_parameter const& aParameter) { return aParameter.kind == script_parameter::Positional && aParameter.type == 'O'; }) ?
                        -1 : static_cast<std::int32_t>(f.parameters.size());
            return f.directParameters;
        }

        // (a call (of aCount positional arguments; 0x100: a method call's (load_method's method, its instance first)) that the program can make 
        // itself (call_indirect): the function of the program it calls, (its index << 1) | 1 if it returns nothing; -1: python::call_arguments's 
        // call (the callable is not one, its arguments are not as it is called with them as they are, or a signal or another thread waits))
        std::int32_t fn_direct_function(object* aCallable, std::int64_t aCount)
        {
            if (aCallable == nullptr)
                return -1;
            bool const methodCall = (aCount & 0x100) != 0;
            auto const count = static_cast<std::size_t>(aCount & 0xFF) + (methodCall ? 1u : 0u);
            object* function = nullptr;
            if (methodCall)
            {
                if (aCallable->type == &sPendingMethodType.base)
                    function = reinterpret_cast<pending_method_object*>(aCallable)->target.classFunction;
            }
            else
                function = aCallable;
            if (function == nullptr || function->type != &sScriptFunctionType.base)
                return -1;
            auto const& f = *as<script_function_object>(function);
            if (direct_parameters(f) != static_cast<std::int32_t>(count) || current_program() != nullptr)
                return -1;
            // (as python::call_arguments: another thread can run; a signal's handler is called by it)
            gil_check();
            if (signals_pending())
                return -1;
            return static_cast<std::int32_t>((f.index << 1u) | (f.result == 'V' ? 1u : 0u));
        }

        // (receiver.name(a0, a1) (the arguments that are not null): load_method's call and python::call_arguments' made at once, with no 
        // method object made for it; a function of the program that is the method (of the receiver's class) that the program can call 
        // itself (see python::direct_function) is not called: ((((its index << 1) | 1 if it returns nothing) + 1) << 1 is (an integer 
        // that is not an object's) for the program to call it with the receiver first; otherwise the call's result (NEOS_RT_INTEGER: an 
        // object's address with its low bit set), or 0 if it raised)
        std::int64_t fn_call_method(object* aReceiver, char const* aName, object* a0, object* a1)
        {
            gil_check();
            if (!signals_check())
                return 0;
            object* const items[] = { a0, a1 };
            std::span<object* const> const arguments{ items, a0 == nullptr ? 0u : a1 == nullptr ? 1u : 2u };
            object* result = nullptr;
            if (method_target target; find_method(aReceiver, aName, target))
            {
                if (target.function != nullptr)
                {
                    pooled_arguments const native{ arguments };
                    result = target.function(aReceiver, native.items, nullptr);
                }
                else if (target.classFunction->type == &sScriptFunctionType.base)
                {
                    auto const& f = *as<script_function_object>(target.classFunction);
                    if (direct_parameters(f) == static_cast<std::int32_t>(arguments.size() + 1u) && current_program() == nullptr && !signals_pending())
                        return static_cast<std::int64_t>(((((static_cast<std::uint64_t>(f.index) << 1u) | (f.result == 'V' ? 1u : 0u)) + 1u) << 1u));
                    ref const function = ref::borrowed(target.classFunction);
                    result = script_function_invoke(target.classFunction, aReceiver, arguments, nullptr);
                }
                else
                {
                    // (a builtin function that binds (its instance first))
                    ref const function = ref::borrowed(target.classFunction);
                    std::vector<object*> bound;
                    bound.reserve(arguments.size() + 1u);
                    bound.push_back((retain(aReceiver), aReceiver));
                    for (auto a : arguments)
                        bound.push_back((retain(a), a));
                    ref const args{ make_tuple(std::move(bound)) };
                    result = call(target.classFunction, args.get(), nullptr);
                }
            }
            else
            {
                ref const attribute{ getattr(aReceiver, aName) };
                if (!attribute)
                {
                    note_attribute_error(aReceiver);
                    return 0;
                }
                result = arguments_call(attribute.get(), arguments, nullptr, nullptr);
            }
            if (checked(result) == nullptr)
                return 0;
            return static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(result) | 1u);
        }

        int instance_setattr(neos_py_context*, object* aSelf, char const* aName, object* aValue)
        {
            return user_setattr(aSelf, as<instance_object>(aSelf)->attributes, aName, aValue);
        }

        char const* binary_method(int aOperation, bool aInplace, bool aReflected)
        {
            static char const* const sNames[][3] =
            {
                { "__add__", "__iadd__", "__radd__" }, { "__sub__", "__isub__", "__rsub__" }, { "__mul__", "__imul__", "__rmul__" },
                { "__truediv__", "__itruediv__", "__rtruediv__" }, { "__floordiv__", "__ifloordiv__", "__rfloordiv__" },
                { "__mod__", "__imod__", "__rmod__" }, { "__pow__", "__ipow__", "__rpow__" }, { "__matmul__", "__imatmul__", "__rmatmul__" },
                { "__lshift__", "__ilshift__", "__rlshift__" }, { "__rshift__", "__irshift__", "__rrshift__" }, { "__and__", "__iand__", "__rand__" },
                { "__or__", "__ior__", "__ror__" }, { "__xor__", "__ixor__", "__rxor__" }
            };
            return sNames[aOperation][aReflected ? 2 : aInplace ? 1 : 0];
        }

        object* user_binary(neos_py_context*, int aOperation, object* aLhs, object* aRhs, int aInplace)
        {
            bool found = false;
            if (user_class_of(aLhs) != nullptr)
            {
                if (aInplace)
                {
                    retain(aRhs);
                    ref r{ call_special(aLhs, binary_method(aOperation, true, false), { aRhs }, found) };
                    if (found && (!r || kind_of(r.get()) != NEOS_PY_NOT_IMPLEMENTED))
                        return r.release_ownership();
                    if (found)
                        return not_implemented();
                }
                retain(aRhs);
                ref r{ call_special(aLhs, binary_method(aOperation, false, false), { aRhs }, found) };
                if (found)
                    return r.release_ownership();
            }
            if (user_class_of(aRhs) != nullptr && user_class_of(aRhs) != user_class_of(aLhs))
            {
                retain(aLhs);
                ref r{ call_special(aRhs, binary_method(aOperation, false, true), { aLhs }, found) };
                if (found)
                    return r.release_ownership();
            }
            // (subclasses of builtin types: their values)
            if (builtin_value(aLhs) != aLhs || builtin_value(aRhs) != aRhs)
            {
                auto const lhs = builtin_value(aLhs);
                ref r{ binary(aOperation, lhs, builtin_value(aRhs), aInplace != 0 && lhs != aLhs) };
                if (r && aInplace != 0 && lhs != aLhs && r.get() == lhs)
                    return (retain(aLhs), aLhs);
                return r.release_ownership();
            }
            return not_implemented();
        }

        object* user_unary(neos_py_context*, int aOperation, object* aSelf)
        {
            static char const* const sNames[] = { "__neg__", "__pos__", "__invert__", "__abs__" };
            bool found = false;
            auto const r = call_special(aSelf, sNames[aOperation], {}, found);
            if (!found && builtin_value(aSelf) != aSelf)
                return unary(aOperation, builtin_value(aSelf));
            if (!found)
                return error("TypeError", std::string{ "bad operand type for unary " } + (aOperation == NEOS_PY_NEGATIVE ? "-" : aOperation == NEOS_PY_POSITIVE ? "+" :
                    aOperation == NEOS_PY_INVERT ? "~" : "abs()") + ": '" + type_name(aSelf) + "'");
            return r;
        }

        object* user_compare(neos_py_context*, int aOperation, object* aLhs, object* aRhs)
        {
            static char const* const sNames[] = { "__lt__", "__le__", "__eq__", "__ne__", "__gt__", "__ge__" };
            static int const sReflected[] = { NEOS_PY_GT, NEOS_PY_GE, NEOS_PY_EQ, NEOS_PY_NE, NEOS_PY_LT, NEOS_PY_LE };
            bool found = false;
            if (user_class_of(aLhs) != nullptr)
            {
                retain(aRhs);
                ref r{ call_special(aLhs, sNames[aOperation], { aRhs }, found) };
                if (found && (!r || kind_of(r.get()) != NEOS_PY_NOT_IMPLEMENTED))
                    return r.release_ownership();
            }
            if (user_class_of(aRhs) != nullptr)
            {
                retain(aLhs);
                ref r{ call_special(aRhs, sNames[sReflected[aOperation]], { aLhs }, found) };
                if (found && (!r || kind_of(r.get()) != NEOS_PY_NOT_IMPLEMENTED))
                    return r.release_ownership();
            }
            if (builtin_value(aLhs) != aLhs || builtin_value(aRhs) != aRhs)
                return compare(aOperation, builtin_value(aLhs), builtin_value(aRhs));
            // (the default: identity; != is the inverse of ==)
            if (aOperation == NEOS_PY_EQ || aOperation == NEOS_PY_NE)
            {
                if (aOperation == NEOS_PY_NE && user_class_of(aLhs) != nullptr && has_special_method(aLhs, "__eq__"))
                {
                    ref const eq{ user_compare(nullptr, NEOS_PY_EQ, aLhs, aRhs) };
                    if (!eq)
                        return nullptr;
                    if (kind_of(eq.get()) == NEOS_PY_NOT_IMPLEMENTED)
                        return make_bool(true);
                    auto const t = truth(eq.get());
                    return t < 0 ? nullptr : make_bool(t == 0);
                }
                return make_bool((aLhs == aRhs) == (aOperation == NEOS_PY_EQ));
            }
            return not_implemented();
        }

        int user_truth(neos_py_context*, object* aSelf)
        {
            bool found = false;
            ref r{ call_special(aSelf, "__bool__", {}, found) };
            if (!found)
            {
                r = ref{ call_special(aSelf, "__len__", {}, found) };
                if (!found)
                    return builtin_value(aSelf) != aSelf ? truth(builtin_value(aSelf)) : 1;
            }
            if (!r)
                return -1;
            return truth(r.get());
        }

        long long user_length(neos_py_context*, object* aSelf)
        {
            bool found = false;
            ref const r{ call_special(aSelf, "__len__", {}, found) };
            if (!found && builtin_value(aSelf) != aSelf)
                return length(builtin_value(aSelf));
            if (!found)
            {
                error("TypeError", "object of type '" + std::string{ type_name(aSelf) } + "' has no len()");
                return -1;
            }
            if (!r)
                return -1;
            long long n = 0;
            if (!to_index(r.get(), n))
                return -1;
            return n;
        }

        object* user_call(neos_py_context*, object* aSelf, object* aArgs, object* aKwargs)
        {
            ref const method{ special_method(aSelf, "__call__") };
            if (!method)
            {
                // (a subclass of a callable type of a module (e.g. weakref.ref): its value's)
                if (auto const value = builtin_value(aSelf); value != aSelf && kind_of(value) == NEOS_PY_OTHER && python_type(value) != nullptr && 
                    python_type(value)->call != nullptr)
                    return call(value, aArgs, aKwargs);
                return error("TypeError", "'" + std::string{ type_name(aSelf) } + "' object is not callable");
            }
            return call(method.get(), aArgs, aKwargs);
        }

        object* user_getitem(neos_py_context*, object* aSelf, object* aKey)
        {
            bool found = false;
            retain(aKey);
            auto const r = call_special(aSelf, "__getitem__", { aKey }, found);
            if (!found && builtin_value(aSelf) != aSelf)
            {
                auto const value = builtin_value(aSelf);
                auto const result = getitem(value, aKey);
                // (a subclass of dict: __missing__)
                if (result == nullptr && kind_of(value) == NEOS_PY_DICT && std::string_view{ error_text() }.starts_with("KeyError"))
                {
                    ref const missing{ special_method(aSelf, "__missing__") };
                    if (missing)
                    {
                        clear_error();
                        retain(aKey);
                        ref const args{ make_tuple({ aKey }) };
                        return call(missing.get(), args.get(), nullptr);
                    }
                }
                return result;
            }
            if (!found)
                return error("TypeError", "'" + std::string{ type_name(aSelf) } + "' object is not subscriptable");
            return r;
        }

        int user_setitem(neos_py_context*, object* aSelf, object* aKey, object* aValue)
        {
            bool found = false;
            retain(aKey);
            ref r;
            if (aValue == nullptr)
                r = ref{ call_special(aSelf, "__delitem__", { aKey }, found) };
            else
            {
                retain(aValue);
                r = ref{ call_special(aSelf, "__setitem__", { aKey, aValue }, found) };
            }
            if (!found && builtin_value(aSelf) != aSelf)
                return setitem(builtin_value(aSelf), aKey, aValue);
            if (!found)
            {
                error("TypeError", "'" + std::string{ type_name(aSelf) } + "' object does not support item " + (aValue == nullptr ? "deletion" : "assignment"));
                return -1;
            }
            return r ? 0 : -1;
        }

        object* user_iter(neos_py_context*, object* aSelf)
        {
            // (__iter__ = None: not iterable)
            if (auto const cls = user_class_of(aSelf))
                if (auto const method = class_lookup(cls, "__iter__"); method != nullptr && kind_of(method) == NEOS_PY_NONE)
                    return error("TypeError", "'" + std::string{ type_name(aSelf) } + "' object is not iterable");
            bool found = false;
            auto const r = call_special(aSelf, "__iter__", {}, found);
            if (!found && builtin_value(aSelf) != aSelf)
                return iter(builtin_value(aSelf));
            if (!found)
            {
                // (a sequence: __getitem__ with 0, 1, ...)
                if (!has_special_method(aSelf, "__getitem__"))
                    return error("TypeError", "'" + std::string{ type_name(aSelf) } + "' object is not iterable");
                std::vector<object*> items;
                for (long long i = 0;; ++i)
                {
                    bool ignored = false;
                    auto const item = call_special(aSelf, "__getitem__", { make_int(i) }, ignored);
                    if (item == nullptr)
                    {
                        if (failed() && error_text() != nullptr && std::string_view{ error_text() }.starts_with("IndexError"))
                        {
                            clear_error();
                            break;
                        }
                        for (auto x : items)
                            release(x);
                        return nullptr;
                    }
                    items.push_back(item);
                }
                ref const l{ make_list(std::move(items)) };
                return iter(l.get());
            }
            return r;
        }

        object* user_next(neos_py_context*, object* aSelf)
        {
            bool found = false;
            auto const r = call_special(aSelf, "__next__", {}, found);
            if (!found)
                return error("TypeError", "'" + std::string{ type_name(aSelf) } + "' object is not an iterator");
            if (r == nullptr && failed() && std::string_view{ error_text() }.starts_with("StopIteration"))
                clear_error();
            return r;
        }

        int user_contains(neos_py_context*, object* aSelf, object* aItem)
        {
            bool found = false;
            retain(aItem);
            ref const r{ call_special(aSelf, "__contains__", { aItem }, found) };
            if (!found && builtin_value(aSelf) != aSelf)
                return contains(builtin_value(aSelf), aItem);
            if (!found)
            {
                ref const iterator{ user_iter(nullptr, aSelf) };
                if (!iterator)
                    return -1;
                for (;;)
                {
                    ref const item{ next(iterator.get()) };
                    if (!item)
                        return failed() ? -1 : 0;
                    ref const equal{ compare(NEOS_PY_EQ, item.get(), aItem) };
                    if (!equal)
                        return -1;
                    if (truth(equal.get()) == 1)
                        return 1;
                }
            }
            return r ? truth(r.get()) : -1;
        }

        int user_index(neos_py_context*, object* aSelf, long long* aResult)
        {
            bool found = false;
            ref const r{ call_special(aSelf, "__index__", {}, found) };
            if (!found && builtin_value(aSelf) != aSelf)
                return to_index(builtin_value(aSelf), *aResult) ? 0 : -1;
            if (!found)
            {
                error("TypeError", "'" + std::string{ type_name(aSelf) } + "' object cannot be interpreted as an integer");
                return -1;
            }
            return r && to_index(r.get(), *aResult) ? 0 : -1;
        }

        int user_to_float(neos_py_context*, object* aSelf, double* aResult)
        {
            bool found = false;
            ref const r{ call_special(aSelf, "__float__", {}, found) };
            if (!found && builtin_value(aSelf) != aSelf)
                return to_double(builtin_value(aSelf), *aResult) ? 0 : -1;
            if (!found)
            {
                error("TypeError", "float() argument must be a string or a real number, not '" + std::string{ type_name(aSelf) } + "'");
                return -1;
            }
            return r && to_double(r.get(), *aResult) ? 0 : -1;
        }

        int user_to_int(neos_py_context*, object* aSelf, long long* aResult)
        {
            bool found = false;
            ref r{ call_special(aSelf, "__int__", {}, found) };
            if (!found)
                r = ref{ call_special(aSelf, "__index__", {}, found) };
            if (!found && builtin_value(aSelf) != aSelf)
            {
                double d = 0.0;
                if (!to_double(builtin_value(aSelf), d))
                    return -1;
                *aResult = static_cast<long long>(d);
                return 0;
            }
            if (!found)
            {
                error("TypeError", "int() argument must be a string, a bytes-like object or a real number, not '" + std::string{ type_name(aSelf) } + "'");
                return -1;
            }
            return r && to_index(r.get(), *aResult) ? 0 : -1;
        }

        void native_disown(object* aClass, object* aValue);

        // (an exception that cannot be raised (e.g. a __del__'s): "Exception ignored in: <aObject>" and its report (sys.unraisablehook's))
        void write_unraisable(object* aObject)
        {
            if (ref const out{ sys_stdout() })
            {
                ref const flushed{ call_method_named(out.get(), "flush", {}) };
                clear_error();
            }
            flush_standard(1);
            ref const e{ take_exception() };
            std::string name;
            if (aObject != nullptr && !repr_of(aObject, name))
                clear_error();
            std::set<object*> seen;
            std::string const report = e ? exception_report(e.get(), seen) : std::string{};
            write_standard(2, "Exception ignored in: " + name + "\n" + report);
        }

        // (an instance is destroyed: its class's __del__ is called (once: it can resurrect it); its weak references are cleared)
        void release_instance(neos_rt_object* aObject)
        {
            auto& instance = *reinterpret_cast<instance_object*>(aObject);
            if (remove_reference(instance.h) != 0)
                return;
            if (!instance.finalized)
                if (auto const del = class_lookup(instance.cls, "__del__"))
                {
                    instance.finalized = true;
                    instance.h.references.store(1, std::memory_order_relaxed);
                    {
                        // (an exception that is being raised is raised again afterwards)
                        ref const pending{ failed() ? take_exception() : ref{} };
                        ref const method{ bind_attribute(del, &instance.h.base, instance.cls) };
                        ref const args{ make_tuple() };
                        ref const r{ method ? call(method.get(), args.get(), nullptr) : nullptr };
                        if (!r)
                            write_unraisable(del);
                        if (pending)
                            fn_raise(pending.get());
                    }
                    if (remove_reference(instance.h) != 0)
                        return;
                }
            if (gWeaklyReferenced.load(std::memory_order_relaxed) != 0u)
                clear_weak_references(&instance.h.base);
            native_disown(instance.cls, instance.value);
            delete &instance;
        }

        neos_py_type make_instance_type()
        {
            auto type = make_type("python.instance", "object", retain_object, release_instance);
            type.repr = instance_repr;
            type.str = instance_str;
            type.getattr = instance_getattr;
            type.setattr = instance_setattr;
            type.call = user_call;
            type.getitem = user_getitem;
            type.setitem = user_setitem;
            type.binary = user_binary;
            type.unary = user_unary;
            type.compare = user_compare;
            type.truth = user_truth;
            type.length = user_length;
            type.iter = user_iter;
            type.next = user_next;
            type.contains = user_contains;
            type.index = user_index;
            type.to_float = user_to_float;
            type.to_int = user_to_int;
            return type;
        }

        // --- classes

        // (the value of an instance (aOwner) of a subclass of a type of a module (aType))
        object* native_construct(object* aType, object* aArgs, object* aKwargs, object* aOwner)
        {
            auto const hooks = native_bases().find(as<type_object>(aType)->qualifiedName);
            if (hooks != native_bases().end() && hooks->second.construct != nullptr)
                return hooks->second.construct(aArgs, aKwargs, aOwner);
            return call(aType, aArgs, aKwargs);
        }

        void native_disown(object* aClass, object* aValue)
        {
            if (aValue == nullptr || !is_class(aClass) || as<class_object>(aClass)->nativeBase == nullptr)
                return;
            auto const hooks = native_bases().find(as<type_object>(as<class_object>(aClass)->nativeBase)->qualifiedName);
            if (hooks != native_bases().end() && hooks->second.disown != nullptr)
                hooks->second.disown(aValue);
        }

        // (a new instance of a class (object.__new__): an instance of a subclass of a builtin type has a value of it: the type 
        // called with aArgs (immutable types), or empty (mutable types: their __init__ sets it))
        object* new_instance(object* aClass, object* aArgs, object* aKwargs)
        {
            // (an abstract class (abc): its abstract methods have no implementation)
            if (auto const abstracts = class_lookup(aClass, "__abstractmethods__"); abstracts != nullptr && length(abstracts) > 0)
            {
                ref const sorted{ b_sorted({ abstracts }, nullptr) };
                if (!sorted)
                    return nullptr;
                auto const& names = as<list_object>(sorted.get())->items;
                std::string list;
                for (std::size_t i = 0u; i < names.size(); ++i)
                    list += (i == 0u ? "'" : ", '") + text_of(names[i]) + "'";
                return error("TypeError", "Can't instantiate abstract class " + as<class_object>(aClass)->name + " without an implementation for abstract method" +
                    (names.size() == 1u ? " " : "s ") + list);
            }
            auto const& base = as<class_object>(aClass)->builtinBase;
            ref value;
            if (!base.empty())
            {
                value = ref{ builtin_construct(base, is_mutable_builtin(base) ? nullptr : aArgs, is_mutable_builtin(base) ? nullptr : aKwargs) };
                if (!value)
                    return nullptr;
            }
            retain(aClass);
            auto const instance = new instance_object{ { { &sInstanceType.base } }, aClass, {} };
            ref result{ &instance->h.base };
            if (auto const native = as<class_object>(aClass)->nativeBase)
            {
                // (a subclass of a type of a module: its value is the type's (of the arguments; of none if those are its __init__'s))
                ref const empty{ make_tuple() };
                value = ref{ native_construct(native, aArgs != nullptr ? aArgs : empty.get(), aKwargs, &instance->h.base) };
                if (!value && class_lookup(aClass, "__init__") != nullptr)
                {
                    ref const raised{ take_exception() };
                    value = ref{ native_construct(native, empty.get(), nullptr, &instance->h.base) };
                    if (!value)
                    {
                        clear_error();
                        fn_raise(raised.get());
                        return nullptr;
                    }
                }
                if (!value)
                    return nullptr;
            }
            instance->value = value.release_ownership();
            return result.release_ownership();
        }

        // (object.__new__(cls, ...), tuple.__new__(cls, ...) etc.)
        // (a native function's one function object (e.g. object.__new__, so that "A.__new__ is object.__new__"): a new reference)
        object* shared_function(char const* aName, native_function aFunction)
        {
            static std::map<std::pair<std::string, native_function>, object*> sFunctions;
            auto& f = sFunctions[{ aName, aFunction }];
            if (f == nullptr)
                f = make_function(aName, aFunction, nullptr);
            return new_reference(f);
        }

        object* builtin_new(neos_py_context*, void*, neos_py_object* aArgs, neos_py_object* aKwargs)
        {
            auto const& args = tuple_items(aArgs);
            if (args.empty())
                return error("TypeError", "object.__new__(): not enough arguments");
            if (!is_class(args[0]))
                return error("TypeError", std::string{ "object.__new__(X): X is not a type object (" } + type_name(args[0]) + ")");
            // (a metaclass's: type.__new__)
            if (as<class_object>(args[0])->builtinBase == "type")
                return fn_type_new(nullptr, nullptr, aArgs, aKwargs);
            ref const rest{ make_tuple(std::vector<object*>{ args.begin() + 1, args.end() }) };
            for (auto a : tuple_items(rest.get()))
                retain(a);
            return new_instance(args[0], rest.get(), aKwargs);
        }

        // (list.__init__(self, ...), dict.__init__(self, ...) etc.: an instance's value (a subclass of a mutable builtin type))
        object* builtin_init(neos_py_context*, void*, neos_py_object* aArgs, neos_py_object* aKwargs)
        {
            auto const& args = tuple_items(aArgs);
            if (args.empty())
                return error("TypeError", "descriptor '__init__' needs an argument");
            if (is_instance(args[0]) && as<instance_object>(args[0])->value != nullptr)
            {
                auto& instance = *as<instance_object>(args[0]);
                auto const& base = as<class_object>(instance.cls)->builtinBase;
                if (auto const native = as<class_object>(instance.cls)->nativeBase)
                {
                    // (a subclass of a type of a module: its value, of the arguments)
                    ref const rest{ make_tuple(std::vector<object*>{ args.begin() + 1, args.end() }) };
                    for (auto a : tuple_items(rest.get()))
                        retain(a);
                    ref value{ native_construct(native, rest.get(), aKwargs, args[0]) };
                    if (!value)
                        return nullptr;
                    native_disown(instance.cls, instance.value);
                    release(instance.value);
                    instance.value = value.release_ownership();
                }
                else if (is_mutable_builtin(base))
                {
                    ref const rest{ make_tuple(std::vector<object*>{ args.begin() + 1, args.end() }) };
                    for (auto a : tuple_items(rest.get()))
                        retain(a);
                    ref value{ builtin_construct(base, rest.get(), aKwargs) };
                    if (!value)
                        return nullptr;
                    release(instance.value);
                    instance.value = value.release_ownership();
                }
            }
            return none();
        }

        object* meta_call(object* aClass, char const* aName, std::vector<object*> aArgs, bool& aFound);

        object* class_repr(neos_py_context*, object* aSelf)
        {
            bool found = false;
            if (auto const r = meta_call(aSelf, "__repr__", {}, found); found)
                return r;
            auto const& cls = *as<class_object>(aSelf);
            return make_str("<class '" + cls.module + "." + (cls.qualname.empty() ? cls.name : cls.qualname) + "'>");
        }

        object* metaclass_call(object* aMetaclass, object* aArgs, object* aKwargs);
        object* class_call_default(object* aSelf, object* aArgs, object* aKwargs);
        object* fn_type_new(neos_py_context*, void*, object* aArgs, object* aKwargs);

        // (object.__init_subclass__(cls): nothing (it takes no keyword arguments))
        object* fn_object_init_subclass(neos_py_context*, void*, object* aArgs, object* aKwargs)
        {
            if (aKwargs != nullptr && kind_of(aKwargs) == NEOS_PY_DICT && !as<dict_object>(aKwargs)->items.empty())
            {
                auto const& args = tuple_items(aArgs);
                std::string const name = !args.empty() && is_class(args[0]) ? as<class_object>(args[0])->name : std::string{ "object" };
                return error("TypeError", name + ".__init_subclass__() takes no keyword arguments");
            }
            return (retain(none()), none());
        }

        object* class_call(neos_py_context*, object* aSelf, object* aArgs, object* aKwargs)
        {
            auto& cls = *as<class_object>(aSelf);
            // (a metaclass: a class; a class whose metaclass has a __call__ of its own: its __call__)
            if (cls.builtinBase == "type")
                return metaclass_call(aSelf, aArgs, aKwargs);
            if (cls.metaclass != nullptr)
                if (auto const metaCall = class_lookup(cls.metaclass, "__call__"))
                {
                    ref const method{ bind_attribute(metaCall, aSelf, cls.metaclass) };
                    return call(method.get(), aArgs, aKwargs);
                }
            return class_call_default(aSelf, aArgs, aKwargs);
        }

        // (type.__call__(cls, ...))
        object* fn_type_call(neos_py_context*, void*, object* aArgs, object* aKwargs)
        {
            auto const& args = tuple_items(aArgs);
            if (args.empty() || !is_class(args[0]))
                return error("TypeError", "descriptor '__call__' requires a 'type' object");
            ref const rest{ make_tuple(std::vector<object*>{ args.begin() + 1, args.end() }) };
            for (auto a : tuple_items(rest.get()))
                retain(a);
            if (as<class_object>(args[0])->builtinBase == "type")
                return metaclass_call(args[0], rest.get(), aKwargs);
            return class_call_default(args[0], rest.get(), aKwargs);
        }

        object* class_call_default(object* aSelf, object* aArgs, object* aKwargs)
        {
            auto& cls = *as<class_object>(aSelf);
            object* self;
            if (!cls.exceptionBase.empty())
            {
                // (an exception: its args are the call's arguments)
                auto args = tuple_items(aArgs);
                for (auto a : args)
                    retain(a);
                self = make_exception(cls.name, std::move(args));
                retain(aSelf);
                as<exception_object>(self)->cls = aSelf;
                if (is_exception_subclass(cls.exceptionBase, "BaseExceptionGroup") && !exception_group_init(self))
                {
                    release(self);
                    return nullptr;
                }
            }
            else if (auto const newMethod = class_lookup(aSelf, "__new__"))
            {
                // (__new__(cls, ...): a static method; __init__ is called if it returns an instance of the class)
                auto const function = newMethod->type == &sWrapperType.base ? as<wrapper_object>(newMethod)->function : newMethod;
                std::vector<object*> args{ (retain(aSelf), aSelf) };
                for (auto a : tuple_items(aArgs))
                    args.push_back((retain(a), a));
                ref const newArgs{ make_tuple(std::move(args)) };
                ref created{ call(function, newArgs.get(), aKwargs) };
                if (!created)
                    return nullptr;
                auto const createdClass = user_class_of(created.get());
                if (createdClass == nullptr || !is_instance(created.get()) || !is_subclass(createdClass, aSelf))
                    return created.release_ownership();
                self = created.release_ownership();
                if (auto const init = class_lookup(aSelf, "__init__"))
                {
                    ref const owned{ self };
                    ref const method{ bind_attribute(init, self, aSelf) };
                    ref const r{ call(method.get(), aArgs, aKwargs) };
                    if (!r)
                        return nullptr;
                    if (kind_of(r.get()) != NEOS_PY_NONE)
                        return error("TypeError", "__init__() should return None, not '" + std::string{ type_name(r.get()) } + "'");
                    return (retain(self), self);
                }
                if (is_mutable_builtin(cls.builtinBase))
                {
                    ref const owned{ self };
                    std::vector<object*> args2{ (retain(self), self) };
                    for (auto a : tuple_items(aArgs))
                        args2.push_back((retain(a), a));
                    ref const initArgs{ make_tuple(std::move(args2)) };
                    ref const r{ builtin_init(nullptr, nullptr, initArgs.get(), aKwargs) };
                    return r ? (retain(self), self) : nullptr;
                }
                return self;
            }
            else
            {
                self = new_instance(aSelf, aArgs, aKwargs);
                if (self == nullptr)
                    return nullptr;
            }
            ref result{ self };
            if (auto const init = class_lookup(aSelf, "__init__"))
            {
                ref const method{ bind_attribute(init, self, aSelf) };
                ref const r{ call(method.get(), aArgs, aKwargs) };
                if (!r)
                    return nullptr;
                if (kind_of(r.get()) != NEOS_PY_NONE)
                    return error("TypeError", "__init__() should return None, not '" + std::string{ type_name(r.get()) } + "'");
            }
            else if (is_mutable_builtin(cls.builtinBase))
            {
                std::vector<object*> args{ (retain(self), self) };
                for (auto a : tuple_items(aArgs))
                    args.push_back((retain(a), a));
                ref const initArgs{ make_tuple(std::move(args)) };
                ref const r{ builtin_init(nullptr, nullptr, initArgs.get(), aKwargs) };
                if (!r)
                    return nullptr;
            }
            else if (cls.exceptionBase.empty() && cls.builtinBase.empty() && cls.nativeBase == nullptr && 
                (tuple_items(aArgs).size() != 0u || (aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty())))
                return error("TypeError", cls.name + "() takes no arguments");
            return result.release_ownership();
        }

        object* class_getattr(neos_py_context*, object* aSelf, char const* aName)
        {
            auto const& cls = *as<class_object>(aSelf);
            std::string_view const name{ aName };
            if (name == "__name__")
                return make_str(cls.name);
            if (name == "__qualname__")
                return make_str(cls.qualname.empty() ? cls.name : cls.qualname);
            if (name == "__class__")
                return class_of(aSelf);
            if (name == "__subclasses__")
                return make_function("__subclasses__", [](neos_py_context*, void*, object* aArgs, object*) -> object*
                {
                    std::vector<object*> result;
                    for (auto c : as<class_object>(tuple_items(aArgs).at(0))->subclasses)
                        result.push_back((retain(c), c));
                    return make_list(std::move(result));
                }, nullptr, aSelf);
            if (name == "__base__")
            {
                if (!cls.bases.empty())
                    return (retain(cls.bases[0]), cls.bases[0]);
                if (cls.nativeBase != nullptr)
                    return new_reference(cls.nativeBase);
                return !cls.builtinBase.empty() ? type_object_for(cls.builtinBase, cls.builtinBase) :
                    cls.exceptionBase.empty() ? type_object_for("object", "object") : exception_class(cls.exceptionBase);
            }
            if (name == "__module__")
                return make_str(cls.module);
            if (name == "__dict__")
            {
                ref const d{ attributes_dict(cls.attributes) };
                return make_mapping_proxy(d.get());
            }
            if (name == "__bases__")
            {
                std::vector<object*> bases;
                for (auto b : cls.bases)
                    bases.push_back((retain(b), b));
                // (its builtin exception classes' bases)
                if (auto const e = user_exception_bases().find(cls.name); !cls.exceptionBase.empty() && e != user_exception_bases().end())
                    for (auto const& n : e->second)
                        if (std::none_of(cls.bases.begin(), cls.bases.end(), [&](object* b) { return as<class_object>(b)->name == n; }))
                            bases.push_back(exception_class(n));
                if (bases.empty())
                    bases.push_back(cls.nativeBase != nullptr ? new_reference(cls.nativeBase) : !cls.builtinBase.empty() ? 
                        type_object_for(cls.builtinBase, cls.builtinBase) : type_object_for("object", "object"));
                return make_tuple(std::move(bases));
            }
            if (name == "__mro__")
            {
                std::vector<object*> mro{ (retain(aSelf), aSelf) };
                for (auto m : cls.mro)
                    mro.push_back((retain(m), m));
                // (the builtin exception classes of its linearization (exception_mro's: the classes' names))
                if (!cls.exceptionBase.empty())
                    for (auto const& n : exception_mro(cls.name))
                        if (n != "object" && n != cls.name && std::none_of(cls.mro.begin(), cls.mro.end(), [&](object* m) { return as<class_object>(m)->name == n; }))
                            mro.push_back(exception_class(n));
                if (!cls.builtinBase.empty())
                    mro.push_back(type_object_for(cls.builtinBase, cls.builtinBase));
                if (cls.nativeBase != nullptr)
                    mro.push_back(new_reference(cls.nativeBase));
                mro.push_back(type_object_for("object", "object"));
                return make_tuple(std::move(mro));
            }
            if (auto const attribute = class_lookup(aSelf, name))
                return bind_attribute(attribute, nullptr, aSelf);
            if (name == "__new__")
                return shared_function("__new__", cls.builtinBase == "type" ? fn_type_new : builtin_new);
            if (name == "__doc__")
                return none();
            if (name == "__init_subclass__")
                return make_function("__init_subclass__", fn_object_init_subclass, nullptr, aSelf);
            if (name == "__call__" && cls.builtinBase == "type")
                return make_function("__call__", fn_type_call, nullptr);
            // (its metaclass's attributes)
            if (cls.metaclass != nullptr)
                if (auto const attribute = class_lookup(cls.metaclass, name))
                    return bind_attribute(attribute, aSelf, cls.metaclass);
            if (!cls.builtinBase.empty())
                if (auto const function = class_function(cls.builtinBase, aName))
                    return function;
            // (object's methods, unbound)
            if (auto const function = class_function("object", aName))
                return function;
            return error("AttributeError", "type object '" + cls.name + "' has no attribute '" + aName + "'");
        }

        // (type.__setattr__ and type.__delattr__ (aValue null): a class's attribute)
        bool linearize(std::vector<object*> const& aBases, std::vector<object*>& aResult);

        // (a class's MRO (and its subclasses'), once its bases have changed)
        bool relinearize(object* aClass)
        {
            auto& cls = *as<class_object>(aClass);
            std::vector<object*> mro;
            if (!linearize(cls.bases, mro))
                return false;
            if (std::find(mro.begin(), mro.end(), aClass) != mro.end())
            {
                error("TypeError", "a __bases__ item causes an inheritance cycle");
                return false;
            }
            for (auto m : mro)
                retain(m);
            for (auto m : cls.mro)
                release(m);
            cls.mro = std::move(mro);
            classes_changed();
            for (auto s : std::vector<object*>{ cls.subclasses })
                if (!relinearize(s))
                    return false;
            return true;
        }

        // (C.__bases__ = (...): classes (and its builtin base, or object))
        int set_class_bases(object* aSelf, object* aValue)
        {
            auto& cls = *as<class_object>(aSelf);
            if (kind_of(aValue) != NEOS_PY_TUPLE)
            {
                error("TypeError", "can only assign tuple to " + cls.name + ".__bases__, not " + type_name(aValue));
                return -1;
            }
            if (as<tuple_object>(aValue)->items.empty())
            {
                error("TypeError", "can only assign non-empty tuple to " + cls.name + ".__bases__, not ()");
                return -1;
            }
            std::vector<object*> bases;
            for (auto b : as<tuple_object>(aValue)->items)
            {
                if (is_class(b))
                {
                    if (b == aSelf)
                    {
                        error("TypeError", "a __bases__ item causes an inheritance cycle");
                        return -1;
                    }
                    bases.push_back(b);
                }
                else if (!is_type_object(b) || !(as<type_object>(b)->name == "object" || as<type_object>(b)->qualifiedName == cls.builtinBase ||
                    (cls.nativeBase != nullptr && b == cls.nativeBase) || (!cls.exceptionBase.empty() && as<type_object>(b)->qualifiedName == cls.exceptionBase)))
                {
                    error("TypeError", "__bases__ assignment: '" + class_name(b) + "' object layout differs from '" + cls.name + "'");
                    return -1;
                }
            }
            auto const previous = cls.bases;
            auto const previousMro = cls.mro;
            for (auto b : previous)
            {
                auto& subclasses = as<class_object>(b)->subclasses;
                subclasses.erase(std::remove(subclasses.begin(), subclasses.end(), aSelf), subclasses.end());
            }
            for (auto b : bases)
                retain(b);
            cls.bases = bases;
            classes_changed();
            for (auto m : previousMro)
                retain(m);
            if (!relinearize(aSelf))
            {
                // (its previous bases restored)
                for (auto b : bases)
                    release(b);
                cls.bases = previous;
                for (auto m : cls.mro)
                    release(m);
                cls.mro = previousMro;
                classes_changed();
                for (auto b : previous)
                    as<class_object>(b)->subclasses.push_back(aSelf);
                return -1;
            }
            for (auto m : previousMro)
                release(m);
            for (auto b : previous)
                release(b);
            for (auto b : bases)
                as<class_object>(b)->subclasses.push_back(aSelf);
            return 0;
        }

        int class_setattr_raw(object* aSelf, char const* aName, object* aValue)
        {
            auto& cls = *as<class_object>(aSelf);
            std::string_view const name{ aName };
            if (aValue != nullptr && name == "__bases__")
                return set_class_bases(aSelf, aValue);
            if (aValue != nullptr && (name == "__name__" || name == "__qualname__" || name == "__module__"))
            {
                if (kind_of(aValue) != NEOS_PY_STR)
                {
                    if (name != "__module__")
                    {
                        error("TypeError", "can only assign string to " + cls.name + "." + std::string{ name } + ", not '" + type_name(aValue) + "'");
                        return -1;
                    }
                }
                else
                {
                    // (its qualified name stays the same when its name changes)
                    if (name == "__name__" && cls.qualname.empty())
                        cls.qualname = cls.name;
                    (name == "__name__" ? cls.name : name == "__qualname__" ? cls.qualname : cls.module) = as<str_object>(aValue)->value;
                    if (name != "__module__")
                        return 0;
                }
            }
            auto& attributes = cls.attributes;
            for (auto a = attributes.begin(); a != attributes.end(); ++a)
                if (a->first == aName)
                {
                    if (aValue == nullptr)
                    {
                        release(a->second);
                        attributes.erase(a);
                        classes_changed();
                        return 0;
                    }
                    retain(aValue);
                    release(a->second);
                    a->second = aValue;
                    return 0;
                }
            if (aValue == nullptr)
            {
                error("AttributeError", "type object '" + cls.name + "' has no attribute '" + aName + "'");
                return -1;
            }
            retain(aValue);
            attributes.emplace_back(aName, aValue);
            classes_changed();
            return 0;
        }

        object* meta_special(object* aClass, char const* aName);

        int class_setattr(neos_py_context*, object* aSelf, char const* aName, object* aValue)
        {
            // (its metaclass's __setattr__ or __delattr__)
            if (ref const hook{ meta_special(aSelf, aValue != nullptr ? "__setattr__" : "__delattr__") })
            {
                ref const args{ aValue != nullptr ? make_tuple({ make_str(aName), new_reference(aValue) }) : make_tuple({ make_str(aName) }) };
                ref const r{ call(hook.get(), args.get(), nullptr) };
                return r ? 0 : -1;
            }
            return class_setattr_raw(aSelf, aName, aValue);
        }

        // (a class's special method from its metaclass (e.g. an enum class's __iter__), bound to the class: null if none)
        object* meta_special(object* aClass, char const* aName)
        {
            auto const metaclass = is_class(aClass) ? as<class_object>(aClass)->metaclass : nullptr;
            if (metaclass == nullptr)
                return nullptr;
            auto const attribute = class_lookup(metaclass, aName);
            if (attribute == nullptr)
                return nullptr;
            return bind_attribute(attribute, aClass, metaclass);
        }

        object* meta_call(object* aClass, char const* aName, std::vector<object*> aArgs, bool& aFound)  // (steals the arguments)
        {
            ref const method{ meta_special(aClass, aName) };
            aFound = static_cast<bool>(method);
            if (!method)
            {
                for (auto a : aArgs)
                    release(a);
                return nullptr;
            }
            ref const args{ make_tuple(std::move(aArgs)) };
            return call(method.get(), args.get(), nullptr);
        }

        object* class_compare(neos_py_context*, int aOperation, object* aLhs, object* aRhs)
        {
            static char const* const sNames[] = { "__lt__", "__le__", "__eq__", "__ne__", "__gt__", "__ge__" };
            bool found = false;
            if (is_class(aLhs))
            {
                ref r{ meta_call(aLhs, sNames[aOperation], { (retain(aRhs), aRhs) }, found) };
                if (found && (!r || kind_of(r.get()) != NEOS_PY_NOT_IMPLEMENTED))
                    return r.release_ownership();
            }
            if (aOperation != NEOS_PY_EQ && aOperation != NEOS_PY_NE)
                return not_implemented();
            return make_bool((aLhs == aRhs) == (aOperation == NEOS_PY_EQ));
        }

        object* class_iter(neos_py_context*, object* aSelf)
        {
            bool found = false;
            auto const r = meta_call(aSelf, "__iter__", {}, found);
            if (!found)
                return error("TypeError", "'type' object is not iterable");
            return r;
        }

        long long class_length(neos_py_context*, object* aSelf)
        {
            bool found = false;
            ref const r{ meta_call(aSelf, "__len__", {}, found) };
            if (!found)
                return (error("TypeError", "object of type 'type' has no len()"), -1);
            if (!r)
                return -1;
            long long n = 0;
            if (!to_index(r.get(), n))
                return -1;
            if (n < 0)
                return (error("ValueError", "__len__() should return >= 0"), -1);
            return n;
        }

        int class_contains(neos_py_context*, object* aSelf, object* aItem)
        {
            bool found = false;
            ref const r{ meta_call(aSelf, "__contains__", { (retain(aItem), aItem) }, found) };
            if (found)
                return r ? truth(r.get()) : -1;
            if (meta_special(aSelf, "__iter__") == nullptr)
                return (error("TypeError", "argument of type 'type' is not iterable"), -1);
            ref const it{ class_iter(nullptr, aSelf) };
            if (!it)
                return -1;
            for (;;)
            {
                ref const item{ next(it.get()) };
                if (!item)
                    return failed() ? -1 : 0;
                auto const e = equal(item.get(), aItem);
                if (e != 0)
                    return e;
            }
        }

        int class_truth(neos_py_context*, object* aSelf)
        {
            bool found = false;
            ref r{ meta_call(aSelf, "__bool__", {}, found) };
            if (found)
                return r ? truth(r.get()) : -1;
            if (ref const len{ meta_special(aSelf, "__len__") })
            {
                auto const n = class_length(nullptr, aSelf);
                return n < 0 ? -1 : n > 0 ? 1 : 0;
            }
            return 1;
        }

        object* class_binary(neos_py_context*, int aOperation, object* aLhs, object* aRhs, int aInplace)
        {
            bool found = false;
            if (is_class(aLhs))
            {
                ref r{ meta_call(aLhs, binary_method(aOperation, aInplace != 0, false), { (retain(aRhs), aRhs) }, found) };
                if (found && (!r || kind_of(r.get()) != NEOS_PY_NOT_IMPLEMENTED))
                    return r.release_ownership();
            }
            if (is_class(aRhs) && aRhs != aLhs)
            {
                ref r{ meta_call(aRhs, binary_method(aOperation, false, true), { (retain(aLhs), aLhs) }, found) };
                if (found && (!r || kind_of(r.get()) != NEOS_PY_NOT_IMPLEMENTED))
                    return r.release_ownership();
            }
            return not_implemented();
        }

        object* class_getitem(neos_py_context*, object* aSelf, object* aKey)
        {
            auto const& cls = *as<class_object>(aSelf);
            if (auto const method = class_lookup(aSelf, "__class_getitem__"))
            {
                ref const bound{ bind_attribute(method, nullptr, aSelf) };
                ref const args{ make_tuple({ (retain(aKey), aKey) }) };
                return call(bound.get(), args.get(), nullptr);
            }
            if (cls.metaclass != nullptr)
                if (auto const method = class_lookup(cls.metaclass, "__getitem__"))
                {
                    ref const bound{ bind_attribute(method, aSelf, cls.metaclass) };
                    ref const args{ make_tuple({ (retain(aKey), aKey) }) };
                    return call(bound.get(), args.get(), nullptr);
                }
            // (a subclass of a generic builtin (e.g. class L(list)): a types.GenericAlias)
            if (cls.builtinBase == "list" || cls.builtinBase == "dict" || cls.builtinBase == "tuple" || cls.builtinBase == "set" || cls.builtinBase == "frozenset")
                return make_generic_alias(aSelf, aKey);
            return error("TypeError", "type '" + cls.name + "' is not subscriptable");
        }

        neos_py_type make_class_type()
        {
            auto type = make_type("python.class", "type", retain_object, release_object<class_object>);
            type.getitem = class_getitem;
            type.repr = class_repr;
            type.call = class_call;
            type.getattr = class_getattr;
            type.setattr = class_setattr;
            type.compare = class_compare;
            type.iter = class_iter;
            type.length = class_length;
            type.contains = class_contains;
            type.truth = class_truth;
            type.binary = class_binary;
            type.str = [](neos_py_context* aContext, object* aSelf) -> object*
            {
                bool found = false;
                if (auto const r = meta_call(aSelf, "__str__", {}, found); found)
                    return r;
                return class_repr(aContext, aSelf);
            };
            return type;
        }

        // (C3 linearization of a class's bases)
        bool linearize(std::vector<object*> const& aBases, std::vector<object*>& aResult)
        {
            std::vector<std::vector<object*>> sequences;
            for (auto b : aBases)
            {
                std::vector<object*> s{ b };
                s.insert(s.end(), as<class_object>(b)->mro.begin(), as<class_object>(b)->mro.end());
                sequences.push_back(std::move(s));
            }
            sequences.push_back(aBases);
            for (;;)
            {
                sequences.erase(std::remove_if(sequences.begin(), sequences.end(), [](auto const& s) { return s.empty(); }), sequences.end());
                if (sequences.empty())
                    return true;
                object* candidate = nullptr;
                for (auto const& s : sequences)
                {
                    auto const head = s.front();
                    bool inTail = false;
                    for (auto const& t : sequences)
                        if (std::find(t.begin() + 1, t.end(), head) != t.end())
                            inTail = true;
                    if (!inTail)
                    {
                        candidate = head;
                        break;
                    }
                }
                if (candidate == nullptr)
                {
                    error("TypeError", "Cannot create a consistent method resolution order (MRO)");
                    return false;
                }
                aResult.push_back(candidate);
                for (auto& s : sequences)
                    if (!s.empty() && s.front() == candidate)
                        s.erase(s.begin());
            }
        }

        std::map<std::string, std::vector<std::string>, std::less<>>& user_exception_bases()
        {
            static std::map<std::string, std::vector<std::string>, std::less<>> sBases;
            return sBases;
        }

        // __class(name, bases, namespace): a class statement
        object* fn_class(char const* aName, object* aBases, object* aNamespace);

        void set_class_instancecheck(object* aClass, int (*aCheck)(object*))
        {
            if (is_class(aClass))
                as<class_object>(aClass)->instancecheck = aCheck;
        }

        object* make_class(std::string const& aModule, std::string const& aName, std::vector<object*> const& aBases, 
            std::vector<std::pair<std::string, object*>> const& aNamespace)
        {
            std::vector<object*> bases;
            for (auto b : aBases)
                bases.push_back((retain(b), b));
            ref const basesTuple{ make_tuple(std::move(bases)) };
            ref const ns{ make_dict() };
            for (auto const& a : aNamespace)
            {
                ref const key{ make_str(a.first) };
                dict_set(ns.get(), key.get(), a.second);
            }
            auto const result = fn_class(aName.c_str(), basesTuple.get(), ns.get());
            if (result != nullptr && is_class(result))
                as<class_object>(result)->module = aModule;
            return result;
        }

        std::map<std::string, module_initializer, std::less<>>& modules()
        {
            static std::map<std::string, module_initializer, std::less<>> sModules;
            return sModules;
        }

        bool register_module(char const* aName, module_initializer aInitializer)
        {
            modules()[aName] = aInitializer;
            return true;
        }

        object* fn_class(char const* aName, object* aBases, object* aNamespace)
        {
            auto* const cls = new class_object{ { { &sClassType.base } }, aName, {}, {}, {}, {} };
            ref result{ &cls->h.base };
            std::vector<std::string> exceptionBases;    // (all of its bases that are exception classes (e.g. class E(ValueError, IndexError)))
            for (auto b : tuple_items(aBases))
            {
                if (is_class(b))
                {
                    retain(b);
                    cls->bases.push_back(b);
                    if (auto const& base = as<class_object>(b)->builtinBase; !base.empty())
                    {
                        if (!cls->builtinBase.empty() && cls->builtinBase != base)
                            return error("TypeError", "multiple bases have instance lay-out conflict");
                        cls->builtinBase = base;
                    }
                    if (auto const native = as<class_object>(b)->nativeBase; native != nullptr && cls->nativeBase != native)
                    {
                        if (cls->nativeBase != nullptr || !cls->builtinBase.empty())
                            return error("TypeError", "multiple bases have instance lay-out conflict");
                        cls->nativeBase = new_reference(native);
                    }
                    if (!as<class_object>(b)->exceptionBase.empty())
                    {
                        if (cls->exceptionBase.empty())
                            cls->exceptionBase = as<class_object>(b)->name;
                        exceptionBases.push_back(as<class_object>(b)->name);
                    }
                }
                else if (is_type_object(b) && as<type_object>(b)->name == "object")
                    ;
                else if (is_type_object(b) && as<type_object>(b)->qualifiedName == "type")
                {
                    // (a metaclass)
                    if (!cls->builtinBase.empty() && cls->builtinBase != "type")
                        return error("TypeError", "multiple bases have instance lay-out conflict");
                    cls->builtinBase = "type";
                }
                else if (is_type_object(b) && is_builtin_base(as<type_object>(b)->name) && as<type_object>(b)->name == as<type_object>(b)->qualifiedName)
                {
                    if (!cls->builtinBase.empty() && cls->builtinBase != as<type_object>(b)->name)
                        return error("TypeError", "multiple bases have instance lay-out conflict");
                    cls->builtinBase = as<type_object>(b)->name;
                }
                else if (is_type_object(b) && is_exception_class(as<type_object>(b)->qualifiedName))
                {
                    if (cls->exceptionBase.empty())
                        cls->exceptionBase = as<type_object>(b)->qualifiedName;
                    exceptionBases.push_back(as<type_object>(b)->qualifiedName);
                }
                else if (is_type_object(b) && as<type_object>(b)->call != nullptr)
                {
                    // (a type of a module (e.g. collections.deque))
                    if ((cls->nativeBase != nullptr && cls->nativeBase != b) || !cls->builtinBase.empty())
                        return error("TypeError", "multiple bases have instance lay-out conflict");
                    if (cls->nativeBase == nullptr)
                        cls->nativeBase = new_reference(b);
                }
                else
                    return error("TypeError", std::string{ "cannot derive class '" } + aName + "' from '" + class_name(b) + "' (not supported)");
            }
            std::vector<object*> mro;
            if (!linearize(cls->bases, mro))
                return nullptr;
            for (auto m : mro)
                cls->mro.push_back((retain(m), m));
            classes_changed();
            for (auto b : cls->bases)
                as<class_object>(b)->subclasses.push_back(&cls->h.base);
            // (its bases' metaclass)
            for (auto b : cls->bases)
                if (auto const m = as<class_object>(b)->metaclass; m != nullptr && (cls->metaclass == nullptr || is_subclass(m, cls->metaclass)))
                {
                    retain(m);
                    release(cls->metaclass);
                    cls->metaclass = m;
                }
            cls->qualname = aName;
            if (!cls->exceptionBase.empty())
                user_exception_bases()[aName] = std::move(exceptionBases);
            // (__slots__: the attributes its instances can have (unless it or a base has none, or they include __dict__))
            {
                bool slotted = true;
                std::vector<std::string> slots;
                for (auto c : cls->mro)
                    if (!as<class_object>(c)->slots)
                        slotted = false;
                    else
                        slots.insert(slots.end(), as<class_object>(c)->slots->begin(), as<class_object>(c)->slots->end());
                ref const key{ make_str("__slots__") };
                auto const position = dict_find(aNamespace, key.get());
                if (position < 0)
                    clear_error();
                if (position >= 0 && slotted && cls->builtinBase.empty() && cls->exceptionBase.empty())
                {
                    auto const value = as<dict_object>(aNamespace)->items[static_cast<std::size_t>(position)].second;
                    std::vector<object*> names;
                    if (kind_of(value) == NEOS_PY_STR)
                        names.push_back((retain(value), value));
                    else if (!collect(value, names))
                        return nullptr;
                    for (auto n : names)
                    {
                        if (kind_of(n) == NEOS_PY_STR)
                            slots.push_back(as<str_object>(n)->value);
                        release(n);
                    }
                    if (std::find(slots.begin(), slots.end(), "__dict__") == slots.end())
                        cls->slots = slots;
                }
            }
            for (auto const& item : as<dict_object>(aNamespace)->items)
            {
                auto const& key = as<str_object>(item.first)->value;
                // (__init_subclass__ and __class_getitem__ are class methods; __new__ is a static method)
                if ((key == "__init_subclass__" || key == "__class_getitem__") && item.second->type == &sScriptFunctionType.base)
                {
                    retain(item.second);
                    cls->attributes.emplace_back(key, &(new wrapper_object{ { { &sWrapperType.base } }, ClassMethod, item.second, nullptr })->h.base);
                    classes_changed();
                    continue;
                }
                retain(item.second);
                cls->attributes.emplace_back(key, item.second);
                classes_changed();
                // (a method's function (_cN_Class_method): its name is the method's)
                auto function = item.second;
                if (function->type == &sWrapperType.base)
                    function = as<wrapper_object>(function)->function;
                if (function != nullptr && function->type == &sScriptFunctionType.base)
                {
                    auto& f = *as<script_function_object>(function);
                    auto const prefix = std::string{ "_" } + aName + "_" + key;
                    if (f.name.starts_with("_c") && f.name.find(prefix) != std::string::npos)
                    {
                        auto const at = f.name.find(prefix) + prefix.size();
                        if (at == f.name.size() || (f.name[at] == '_' && std::all_of(f.name.begin() + static_cast<std::ptrdiff_t>(at) + 1, f.name.end(), 
                            [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; })))
                        {
                            f.name = key;
                            if (f.qualname.empty())
                                f.qualname = std::string{ aName } + "." + key;
                        }
                    }
                }
            }
            return result.release_ownership();
        }

        // --- class creation: type.__new__ (type_new) and class statements (__build_class)

        // (the items of a namespace (a dict, or another mapping) as a dict: a new reference)
        object* namespace_dict(object* aNamespace)
        {
            if (kind_of(aNamespace) == NEOS_PY_DICT)
            {
                ref result{ make_dict() };
                for (auto const& item : as<dict_object>(aNamespace)->items)
                    if (dict_set(result.get(), item.first, item.second) < 0)
                        return nullptr;
                return result.release_ownership();
            }
            ref result{ make_dict() };
            if (!dict_update(result.get(), aNamespace))
                return nullptr;
            return result.release_ownership();
        }

        // (a dict's item (borrowed), removed: null if none)
        ref dict_pop(object* aDict, char const* aKey)
        {
            ref const key{ make_str(aKey) };
            auto const position = dict_find(aDict, key.get());
            if (position < 0)
            {
                clear_error();
                return ref{};
            }
            auto& d = *as<dict_object>(aDict);
            ref value = ref::borrowed(d.items[static_cast<std::size_t>(position)].second);
            dict_erase(d, static_cast<std::size_t>(position));
            return value;
        }

        // type.__new__(metatype, name, bases, namespace, **kwargs): a class (its names set (__set_name__), its base's 
        // __init_subclass__ called, its cell (__classcell__) set)
        object* type_new(object* aMetatype, std::string const& aName, object* aBases, object* aNamespace, object* aKwargs)
        {
            if (kind_of(aBases) != NEOS_PY_TUPLE)
                return error("TypeError", std::string{ "type.__new__() argument 2 must be tuple, not " } + type_name(aBases));
            ref const ns{ namespace_dict(aNamespace) };
            if (!ns)
                return nullptr;
            auto const cell = dict_pop(ns.get(), "__classcell__");
            auto const qualname = dict_pop(ns.get(), "__qualname__");
            // (a class that defines __eq__ but not __hash__: its __hash__ is None (type_new's: it is unhashable))
            {
                ref const eqKey{ make_str("__eq__") };
                ref const hashKey{ make_str("__hash__") };
                if (dict_find(ns.get(), eqKey.get()) >= 0 && dict_find(ns.get(), hashKey.get()) == -1 && dict_set(ns.get(), hashKey.get(), none()) < 0)
                    return nullptr;
                clear_error();
            }
            ref const module{ [&]() -> object* { ref const key{ make_str("__module__") }; auto const p = dict_find(ns.get(), key.get());
                if (p < 0) { clear_error(); return nullptr; } auto const v = as<dict_object>(ns.get())->items[static_cast<std::size_t>(p)].second; retain(v); return v; }() };
            ref cls{ fn_class(aName.c_str(), aBases, ns.get()) };
            if (!cls)
                return nullptr;
            auto& c = *as<class_object>(cls.get());
            if (qualname && kind_of(qualname.get()) == NEOS_PY_STR)
                c.qualname = as<str_object>(qualname.get())->value;
            if (module && kind_of(module.get()) == NEOS_PY_STR)
                c.module = as<str_object>(module.get())->value;
            if (aMetatype != nullptr && is_class(aMetatype))
            {
                retain(aMetatype);
                release(c.metaclass);
                c.metaclass = aMetatype;
            }
            // (its attributes' __set_name__)
            auto const attributes = c.attributes;
            for (auto const& a : attributes)
                if (is_instance(a.second))
                    if (ref const setName{ special_method(a.second, "__set_name__") })
                    {
                        ref const args{ make_tuple({ (retain(cls.get()), cls.get()), make_str(a.first) }) };
                        ref const r{ call(setName.get(), args.get(), nullptr) };
                        if (!r)
                            return nullptr;
                    }
            // (its base's __init_subclass__)
            bool initialized = false;
            for (auto m : c.mro)
            {
                object* found = nullptr;
                for (auto const& a : as<class_object>(m)->attributes)
                    if (a.first == "__init_subclass__")
                        found = a.second;
                if (found == nullptr)
                    continue;
                ref const method{ bind_attribute(found, cls.get(), cls.get()) };
                ref const args{ make_tuple() };
                ref const r{ call(method.get(), args.get(), aKwargs) };
                if (!r)
                    return nullptr;
                initialized = true;
                break;
            }
            if (!initialized && aKwargs != nullptr && kind_of(aKwargs) == NEOS_PY_DICT && !as<dict_object>(aKwargs)->items.empty())
                return error("TypeError", aName + ".__init_subclass__() takes no keyword arguments");
            if (cell)
            {
                if (setattr(cell.get(), "cell_contents", cls.get()) < 0)
                    return nullptr;
            }
            return cls.release_ownership();
        }

        // (type.__new__: (metatype, name, bases, namespace) or type(object))
        object* fn_type_new(neos_py_context*, void*, object* aArgs, object* aKwargs)
        {
            auto const& args = tuple_items(aArgs);
            if (args.size() != 4u || kind_of(args[1]) != NEOS_PY_STR)
                return error("TypeError", "type.__new__() takes exactly 3 arguments (" + std::to_string(args.empty() ? 0u : args.size() - 1u) + " given)");
            return type_new(args[0], as<str_object>(args[1])->value, args[2], args[3], aKwargs);
        }

        // (a metaclass's instance: its __new__ (or type.__new__), then its __init__)
        object* metaclass_call(object* aMetaclass, object* aArgs, object* aKwargs)
        {
            auto const& args = tuple_items(aArgs);
            ref created;
            if (auto const newMethod = class_lookup(aMetaclass, "__new__"))
            {
                auto const function = newMethod->type == &sWrapperType.base ? as<wrapper_object>(newMethod)->function : newMethod;
                std::vector<object*> items{ (retain(aMetaclass), aMetaclass) };
                for (auto a : args)
                    items.push_back((retain(a), a));
                ref const newArgs{ make_tuple(std::move(items)) };
                created = ref{ call(function, newArgs.get(), aKwargs) };
            }
            else
            {
                if (args.size() != 3u || kind_of(args[0]) != NEOS_PY_STR)
                    return error("TypeError", "type() takes 1 or 3 arguments");
                created = ref{ type_new(aMetaclass, as<str_object>(args[0])->value, args[1], args[2], aKwargs) };
            }
            if (!created)
                return nullptr;
            if (is_class(created.get()) && as<class_object>(created.get())->metaclass != nullptr && is_subclass(as<class_object>(created.get())->metaclass, aMetaclass))
                if (auto const init = class_lookup(aMetaclass, "__init__"))
                {
                    ref const method{ bind_attribute(init, created.get(), aMetaclass) };
                    ref const r{ call(method.get(), aArgs, aKwargs) };
                    if (!r)
                        return nullptr;
                }
            return created.release_ownership();
        }

        // __build_class(body): a class statement's class (body: the closure of its body's function (see the Python compiler's 
        // statements.cpp), whose default values are its bases (__bases__) and keywords (__kw__))
        // (the namespaces of the class bodies being run (innermost last), and the names bound through them)
        struct class_namespace
        {
            object* ns;
            std::set<std::string> bound;
        };
        thread_local std::vector<class_namespace> tClassNamespaces;

        // __ns_bind(name, value): a class body's assignment of a name: its namespace's item (a namespace that is not a dict's (e.g. 
        // an enumeration's EnumDict) can make another value), and the value the name is bound to
        object* b_ns_bind(std::vector<object*> const& aArgs, object*)
        {
            if (aArgs.size() != 2u || kind_of(aArgs[0]) != NEOS_PY_STR)
                return error("SystemError", "__ns_bind(name, value)");
            if (tClassNamespaces.empty() || kind_of(tClassNamespaces.back().ns) == NEOS_PY_DICT)
                return new_reference(aArgs[1]);
            auto& current = tClassNamespaces.back();
            if (setitem(current.ns, aArgs[0], aArgs[1]) < 0)
                return nullptr;
            current.bound.insert(as<str_object>(aArgs[0])->value);
            ref value{ getitem(current.ns, aArgs[0]) };
            if (!value)
            {
                clear_error();
                return new_reference(aArgs[1]);
            }
            return value.release_ownership();
        }

        object* b_build_class(std::vector<object*> const& aArgs, object*)
        {
            if (aArgs.size() != 1u || aArgs[0]->type != &sScriptFunctionType.base)
                return error("SystemError", "__build_class(body)");
            auto const& body = *as<script_function_object>(aArgs[0]);
            object* bases = nullptr;
            object* keywords = nullptr;
            for (auto const& p : body.parameters)
                if (p.name == "__bases__")
                    bases = p.defaultValue;
                else if (p.name == "__kw__")
                    keywords = p.defaultValue;
            ref const empty{ make_tuple() };
            if (bases == nullptr || kind_of(bases) != NEOS_PY_TUPLE)
                bases = empty.get();
            // (PEP 560: a base that is not a class (e.g. typing.Generic[T]) is the bases its __mro_entries__(bases) gives; the bases as they
            // were written are then the class's __orig_bases__)
            ref const originalBases{ ref::borrowed(bases) };
            ref resolvedBases;
            {
                bool changed = false;
                std::vector<object*> resolved;
                for (auto b : tuple_items(bases))
                {
                    if (!is_class(b) && !is_type_object(b))
                    {
                        ref const entries{ getattr(b, "__mro_entries__") };
                        if (entries)
                        {
                            ref const entryArgs{ make_tuple({ new_reference(bases) }) };
                            ref const result{ call(entries.get(), entryArgs.get(), nullptr) };
                            if (!result || kind_of(result.get()) != NEOS_PY_TUPLE)
                            {
                                for (auto r : resolved)
                                    release(r);
                                return result ? error("TypeError", "__mro_entries__ must return a tuple") : nullptr;
                            }
                            for (auto e : tuple_items(result.get()))
                                resolved.push_back(new_reference(e));
                            changed = true;
                            continue;
                        }
                        clear_error();
                    }
                    resolved.push_back(new_reference(b));
                }
                resolvedBases = ref{ make_tuple(std::move(resolved)) };
                if (changed)
                    bases = resolvedBases.get();
            }
            bool const basesChanged = bases != originalBases.get();
            ref kwargs{ make_dict() };
            if (keywords != nullptr && kind_of(keywords) == NEOS_PY_DICT)
                for (auto const& item : as<dict_object>(keywords)->items)
                    dict_set(kwargs.get(), item.first, item.second);
            auto const explicitMetaclass = dict_pop(kwargs.get(), "metaclass");
            auto const& name = body.name;
            // (the metaclass: the most derived of the explicit one and the bases' metaclasses)
            object* metaclass = explicitMetaclass ? explicitMetaclass.get() : nullptr;
            bool const callable = metaclass != nullptr && !is_class(metaclass) && !is_type_object(metaclass);
            if (metaclass != nullptr && is_type_object(metaclass) && as<type_object>(metaclass)->qualifiedName == "type")
                metaclass = nullptr;
            if (!callable)
                for (auto b : tuple_items(bases))
                {
                    auto const m = is_class(b) ? as<class_object>(b)->metaclass : nullptr;
                    if (m == nullptr)
                        continue;
                    if (metaclass == nullptr || is_subclass(m, metaclass))
                        metaclass = m;
                    else if (!is_subclass(metaclass, m))
                        return error("TypeError", "metaclass conflict: the metaclass of a derived class must be a (non-strict) subclass of the metaclasses of all its bases");
                }
            // (its namespace: the metaclass's __prepare__, or a dict)
            ref ns;
            if (metaclass != nullptr && is_class(metaclass))
                if (auto const prepare = class_lookup(metaclass, "__prepare__"))
                {
                    ref const method{ bind_attribute(prepare, nullptr, metaclass) };
                    ref const args{ make_tuple({ make_str(name), (retain(bases), bases) }) };
                    ns = ref{ call(method.get(), args.get(), kwargs.get()) };
                    if (!ns)
                        return nullptr;
                }
            if (!ns)
                ns = ref{ make_dict() };
            auto const set = [&](object* aKey, object* aValue) { return setitem(ns.get(), aKey, aValue) >= 0; };
            {
                ref const moduleKey{ make_str("__module__") };
                ref const moduleName{ make_str(body.module) };
                ref const qualnameKey{ make_str("__qualname__") };
                ref const qualname{ make_str(body.qualname.empty() ? name : body.qualname) };
                if (!set(moduleKey.get(), moduleName.get()) || !set(qualnameKey.get(), qualname.get()))
                    return nullptr;
                // (__firstlineno__: its first line (its body's function's, as the host knows it))
                if (auto const* h = host(); h != nullptr && h->version >= 6u && h->function_frame != nullptr)
                {
                    neos_rt_frame frame{};
                    if (h->function_frame(program_context(body.program), body.index, &frame) != 0u && frame.firstLine != 0u)
                    {
                        ref const key{ make_str("__firstlineno__") };
                        ref const line{ make_int(frame.firstLine) };
                        if (!set(key.get(), line.get()))
                            return nullptr;
                    }
                }
                for (auto const& a : body.attributes)
                    if (a.first == "__doc__")
                    {
                        ref const docKey{ make_str("__doc__") };
                        if (!set(docKey.get(), a.second))
                            return nullptr;
                    }
            }
            // (the body: the names it binds (except those it bound through its namespace))
            ref const noArgs{ make_tuple() };
            tClassNamespaces.push_back({ ns.get(), {} });
            ref const names{ call(aArgs[0], noArgs.get(), nullptr) };
            auto const bound = std::move(tClassNamespaces.back().bound);
            tClassNamespaces.pop_back();
            if (!names)
                return nullptr;
            if (kind_of(names.get()) == NEOS_PY_DICT)
                for (auto const& item : as<dict_object>(names.get())->items)
                    if ((kind_of(item.first) != NEOS_PY_STR || !bound.contains(as<str_object>(item.first)->value)) && !set(item.first, item.second))
                        return nullptr;
            if (basesChanged)
            {
                ref const key{ make_str("__orig_bases__") };
                if (!set(key.get(), originalBases.get()))
                    return nullptr;
            }
            if (callable)
            {
                ref const args{ make_tuple({ make_str(name), (retain(bases), bases), (retain(ns.get()), ns.get()) }) };
                return call(explicitMetaclass.get(), args.get(), kwargs.get());
            }
            if (metaclass != nullptr)
            {
                ref const args{ make_tuple({ make_str(name), (retain(bases), bases), (retain(ns.get()), ns.get()) }) };
                return call(metaclass, args.get(), kwargs.get());
            }
            return type_new(nullptr, name, bases, ns.get(), kwargs.get());
        }

        // --- bound methods, staticmethod, classmethod, property and super

        neos_py_type make_bound_method_type()
        {
            auto type = make_type("python.method", "method", retain_object, release_object<bound_method_object>);
            type.repr = [](neos_py_context*, object* aSelf) -> object*
            {
                auto const& m = *as<bound_method_object>(aSelf);
                std::string name;
                ref const n{ getattr(m.function, "__name__") };
                if (n && kind_of(n.get()) == NEOS_PY_STR)
                    name = as<str_object>(n.get())->value;
                else
                    clear_error();
                std::string self;
                if (!repr_of(m.self, self))
                    return nullptr;
                auto const cls = is_class(m.self) ? m.self : user_class_of(m.self);
                return make_str("<bound method " + (cls != nullptr ? as<class_object>(cls)->name + "." : std::string{}) + name + " of " + self + ">");
            };
            type.call = [](neos_py_context*, object* aSelf, object* aArgs, object* aKwargs) -> object*
            {
                auto const& m = *as<bound_method_object>(aSelf);
                std::vector<object*> items{ (retain(m.self), m.self) };
                for (auto a : tuple_items(aArgs))
                    items.push_back((retain(a), a));
                ref const args{ make_tuple(std::move(items)) };
                return call(m.function, args.get(), aKwargs);
            };
            // (bound methods are equal if their selves are identical and their functions equal (method_richcompare))
            long long pointer_hash(void const* aPointer);
            type.compare = [](neos_py_context*, int aOperation, object* aLhs, object* aRhs) -> object*
            {
                if ((aOperation != NEOS_PY_EQ && aOperation != NEOS_PY_NE) || aLhs->type != aRhs->type)
                    return not_implemented();
                auto const& a = *as<bound_method_object>(aLhs);
                auto const& b = *as<bound_method_object>(aRhs);
                int e = a.self == b.self ? equal(a.function, b.function) : 0;
                if (e < 0)
                    return nullptr;
                return make_bool((e == 1) == (aOperation == NEOS_PY_EQ));
            };
            type.hash = [](neos_py_context*, object* aSelf, long long* aResult) -> int
            {
                auto const& m = *as<bound_method_object>(aSelf);
                long long h = 0;
                if (!hash_of(m.function, h))
                    return -1;
                *aResult = pointer_hash(m.self) ^ h;
                if (*aResult == -1)
                    *aResult = -2;
                return 0;
            };
            type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
            {
                auto const& m = *as<bound_method_object>(aSelf);
                if (std::strcmp(aName, "__self__") == 0)
                    return (retain(m.self), m.self);
                if (std::strcmp(aName, "__func__") == 0)
                    return (retain(m.function), m.function);
                return getattr(m.function, aName);
            };
            return type;
        }

        neos_py_type make_wrapper_type()
        {
            auto type = make_type("python.wrapper", "property", retain_object, release_object<wrapper_object>);
            type.repr = [](neos_py_context*, object* aSelf) -> object*
            {
                static char const* const sNames[] = { "staticmethod", "classmethod", "property" };
                return make_str("<" + std::string{ sNames[as<wrapper_object>(aSelf)->kind] } + " object at " + object_address(aSelf) + ">");
            };
            type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
            {
                auto& w = *as<wrapper_object>(aSelf);
                if (w.kind == Property && (std::strcmp(aName, "setter") == 0 || std::strcmp(aName, "deleter") == 0 || std::strcmp(aName, "getter") == 0))
                {
                    // (property.setter(f), property.deleter(f), property.getter(f): a copy of the property with that function)
                    static int const sWhich[] = { 0, 1, 2 };
                    int const* which = aName[0] == 'g' ? &sWhich[0] : aName[0] == 's' ? &sWhich[1] : &sWhich[2];
                    return make_function(aName, [](neos_py_context*, void* aData, object* aArgs, object*) -> object*
                    {
                        auto const& args = tuple_items(aArgs);
                        if (args.size() != 2u)
                            return error("TypeError", "expected one argument");
                        auto const& p = *as<wrapper_object>(args[0]);
                        auto const which = *static_cast<int const*>(aData);
                        object* const functions[] = { which == 0 ? args[1] : p.function, which == 1 ? args[1] : p.setter, which == 2 ? args[1] : p.deleter };
                        for (auto f : functions)
                            retain(f);
                        auto* const copy = new wrapper_object{ { { &sWrapperType.base } }, Property, functions[0], functions[1], functions[2] };
                        if (p.doc != nullptr)
                            copy->doc = (retain(p.doc), p.doc);
                        return &copy->h.base;
                    }, const_cast<int*>(which), aSelf);
                }
                if (std::strcmp(aName, "fdel") == 0)
                    return w.deleter != nullptr ? (retain(w.deleter), w.deleter) : none();
                if (std::strcmp(aName, "__get__") == 0 && w.kind == Property)
                {
                    return make_function("__get__", [](neos_py_context*, void*, object* aArgs, object*) -> object*
                    {
                        auto const& args = tuple_items(aArgs);
                        if (args.size() < 2u || kind_of(args[1]) == NEOS_PY_NONE)
                            return args.empty() ? none() : (retain(args[0]), args[0]);
                        ref const call_args{ make_tuple({ (retain(args[1]), args[1]) }) };
                        return call(as<wrapper_object>(args[0])->function, call_args.get(), nullptr);
                    }, nullptr, aSelf);
                }
                // (abstract (abc.abstractmethod): if its function (any of a property's functions) is)
                if (std::strcmp(aName, "__isabstractmethod__") == 0)
                {
                    for (auto f : { w.function, w.setter, w.deleter })
                        if (f != nullptr)
                        {
                            ref const flag{ getattr(f, "__isabstractmethod__") };
                            if (flag && truth(flag.get()) == 1)
                                return make_bool(true);
                            clear_error();
                        }
                    return make_bool(false);
                }
                if (std::strcmp(aName, "__func__") == 0 || std::strcmp(aName, "fget") == 0)
                    return (retain(w.function), w.function);
                if (std::strcmp(aName, "fset") == 0)
                    return w.setter != nullptr ? (retain(w.setter), w.setter) : none();
                // (a property's doc: its own (given or set), if it has one)
                if (std::strcmp(aName, "__doc__") == 0 && w.kind == Property && w.doc != nullptr)
                    return (retain(w.doc), w.doc);
                // (its doc and name: its function's)
                if (std::strcmp(aName, "__doc__") == 0 || (std::strcmp(aName, "__name__") == 0 && w.kind != StaticMethod && w.kind != ClassMethod) ||
                    (std::strcmp(aName, "__wrapped__") == 0 && (w.kind == StaticMethod || w.kind == ClassMethod)))
                {
                    if (std::strcmp(aName, "__wrapped__") == 0)
                        return (retain(w.function), w.function);
                    if (auto const value = getattr(w.function, aName))
                        return value;
                    if (std::strcmp(aName, "__doc__") != 0)
                        return nullptr;
                    clear_error();
                    return (retain(none()), none());
                }
                return error("AttributeError", "'property' object has no attribute '" + std::string{ aName } + "'");
            };
            // (a property's __doc__ can be set (e.g. collections.namedtuple's fields'))
            type.setattr = [](neos_py_context*, object* aSelf, char const* aName, object* aValue) -> int
            {
                auto& w = *as<wrapper_object>(aSelf);
                if (w.kind == Property && std::strcmp(aName, "__doc__") == 0)
                {
                    retain(aValue);
                    release(w.doc);
                    w.doc = aValue;
                    return 0;
                }
                error("AttributeError", "'property' object attribute '" + std::string{ aName } + "' is read-only");
                return -1;
            };
            type.call = [](neos_py_context*, object* aSelf, object* aArgs, object* aKwargs) -> object*
            {
                auto const& w = *as<wrapper_object>(aSelf);
                if (w.kind != StaticMethod)
                    return error("TypeError", "'property' object is not callable");
                return call(w.function, aArgs, aKwargs);
            };
            return type;
        }

        object* make_wrapper(int aKind, std::vector<object*> const& aArgs, char const* aName)
        {
            if (aArgs.size() != 1u && !(aKind == Property && aArgs.size() <= 4u))
                return error("TypeError", std::string{ aName } + " expected 1 argument, got " + std::to_string(aArgs.size()));
            auto const function = [&](std::size_t i) -> object* { return i < aArgs.size() && kind_of(aArgs[i]) != NEOS_PY_NONE ? (retain(aArgs[i]), aArgs[i]) : nullptr; };
            auto* const w = new wrapper_object{ { { &sWrapperType.base } }, aKind, aArgs.empty() ? (retain(none()), none()) : (retain(aArgs[0]), aArgs[0]), function(1u), function(2u) };
            // (property(fget, fset, fdel, doc): its doc)
            if (aKind == Property)
                w->doc = function(3u);
            return &w->h.base;
        }

        object* b_staticmethod(std::vector<object*> const& aArgs, object*) { return make_wrapper(StaticMethod, aArgs, "staticmethod"); }
        object* b_classmethod(std::vector<object*> const& aArgs, object*) { return make_wrapper(ClassMethod, aArgs, "classmethod"); }
        object* b_property(std::vector<object*> const& aArgs, object*) { return make_wrapper(Property, aArgs, "property"); }
        object* b_cached_property(std::vector<object*> const& aArgs, object*) { return make_wrapper(CachedProperty, aArgs, "cached_property"); }

        object* slot_function(std::string const& aClass, char const* aName);

        neos_py_type make_super_type()
        {
            auto type = make_type("python.super", "super", retain_object, release_object<super_object>);
            type.repr = [](neos_py_context*, object* aSelf) -> object*
            {
                return make_str("<super: <class '" + as<class_object>(as<super_object>(aSelf)->cls)->name + "'>, <" + type_name(as<super_object>(aSelf)->self) + " object>>");
            };
            type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
            {
                auto const& s = *as<super_object>(aSelf);
                // (super(cls, subclass) (e.g. in a class method): the subclass's MRO; super(cls, instance) (an instance of a class, or 
                // a class (an instance of a metaclass)): its class's)
                bool const classSelf = is_class(s.self) && is_class(s.cls) && is_subclass(s.self, s.cls);
                auto const selfClass = classSelf ? s.self : is_class(s.self) ? as<class_object>(s.self)->metaclass : user_class_of(s.self);
                auto const bound = classSelf ? nullptr : s.self;
                std::vector<object*> mro;
                if (selfClass != nullptr)
                {
                    mro.push_back(selfClass);
                    mro.insert(mro.end(), as<class_object>(selfClass)->mro.begin(), as<class_object>(selfClass)->mro.end());
                }
                auto position = std::find(mro.begin(), mro.end(), s.cls);
                if (position != mro.end())
                    ++position;
                for (; position != mro.end(); ++position)
                    for (auto const& a : as<class_object>(*position)->attributes)
                        if (a.first == aName)
                            return bind_attribute(a.second, bound, selfClass);
                // (a type of a module that the class derives from (e.g. types.GenericAlias): its special methods, of the instance's value)
                if (selfClass != nullptr && as<class_object>(selfClass)->nativeBase != nullptr && bound != nullptr)
                    if (ref const slot{ slot_function(as<type_object>(as<class_object>(selfClass)->nativeBase)->qualifiedName, aName) })
                        return make_bound_method(slot.get(), bound);
                // (type's and object's methods)
                bool const metaclassSelf = is_class(s.self) && !classSelf;
                if (metaclassSelf && std::strcmp(aName, "__call__") == 0)
                    return make_function("__call__", fn_type_call, nullptr, s.self);
                if (std::strcmp(aName, "__init_subclass__") == 0)
                    return make_function("__init_subclass__", fn_object_init_subclass, nullptr, s.self);
                if (std::strcmp(aName, "__getattribute__") == 0 || std::strcmp(aName, "__setattr__") == 0 || std::strcmp(aName, "__delattr__") == 0)
                    if (auto const function = class_function("object", aName))
                    {
                        ref const unbound{ function };
                        return make_function(aName, as<function_object>(function)->function, as<function_object>(function)->data, s.self);
                    }
                // (object's __new__; a builtin base type's methods (e.g. dict.__setitem__))
                if (std::strcmp(aName, "__new__") == 0)
                    return make_function("__new__", builtin_new, nullptr);
                if (auto const value = builtin_value(s.self); value != s.self && std::strcmp(aName, "__init__") != 0)
                {
                    if (auto const method = protocol_method(value, kind_of(value), aName))
                        return method;
                    return getattr(value, aName);
                }
                if (std::strcmp(aName, "__init__") == 0 && builtin_value(s.self) != s.self)
                    return make_function("__init__", builtin_init, nullptr, s.self);
                // (object's and the builtin exceptions' __init__)
                if (std::strcmp(aName, "__init__") == 0)
                {
                    retain(s.self);
                    return make_function("__init__", [](neos_py_context*, void*, object* aArgs, object*) -> object*
                    {
                        auto const& args = tuple_items(aArgs);
                        if (!args.empty() && args[0]->type == &sExceptionType.base)
                        {
                            auto& e = *as<exception_object>(args[0]);
                            for (auto a : e.args)
                                release(a);
                            e.args.assign(args.begin() + 1, args.end());
                            for (auto a : e.args)
                                retain(a);
                        }
                        return none();
                    }, nullptr, s.self);
                }
                if (std::strcmp(aName, "__str__") == 0 || std::strcmp(aName, "__repr__") == 0)
                {
                    retain(s.self);
                    bool const isStr = std::strcmp(aName, "__str__") == 0;
                    return make_function(aName, isStr ?
                        static_cast<native_function>([](neos_py_context*, void*, object* aArgs, object*) -> object*
                        {
                            auto const self = tuple_items(aArgs).at(0);
                            if (self->type == &sExceptionType.base)
                            {
                                std::string text;
                                return exception_text(self, text) ? make_str(text) : nullptr;
                            }
                            return make_str("<__main__." + as<class_object>(user_class_of(self))->name + " object at " + object_address(self) + ">");
                        }) :
                        static_cast<native_function>([](neos_py_context*, void*, object* aArgs, object*) -> object*
                        {
                            auto const self = tuple_items(aArgs).at(0);
                            if (self->type == &sExceptionType.base)
                                return exception_repr(nullptr, self);
                            return make_str("<__main__." + as<class_object>(user_class_of(self))->name + " object at " + object_address(self) + ">");
                        }), nullptr, s.self);
                }
                return error("AttributeError", "'super' object has no attribute '" + std::string{ aName } + "'");
            };
            return type;
        }

        object* b_super(std::vector<object*> const& aArgs, object*)
        {
            if (aArgs.size() != 2u || !is_class(aArgs[0]))
                return error("RuntimeError", "super(): no arguments");
            retain(aArgs[0]);
            retain(aArgs[1]);
            return &(new super_object{ { { &sSuperType.base } }, aArgs[0], aArgs[1] })->h.base;
        }

        object* meta_special(object* aClass, char const* aName);

        // (isinstance's and issubclass's second argument: its classes (borrowed), from tuples and unions; those whose metaclass has
        // the check (e.g. __instancecheck__) are checked by it (1: true, -1: an exception))
        int class_info(object* aInfo, object* aValue, char const* aCheck, std::vector<object*>& aClasses)
        {
            if (kind_of(aInfo) == NEOS_PY_TUPLE || is_union(aInfo))
            {
                for (auto c : as<tuple_object>(kind_of(aInfo) == NEOS_PY_TUPLE ? aInfo : union_args(aInfo))->items)
                    if (auto const r = class_info(c, aValue, aCheck, aClasses); r != 0)
                        return r;
                return 0;
            }
            if (is_class(aInfo) && (aCheck[2] != 'i' || user_class_of(aValue) != aInfo))
                if (ref const check{ meta_special(aInfo, aCheck) })
                {
                    ref const args{ make_tuple({ (retain(aValue), aValue) }) };
                    ref const r{ call(check.get(), args.get(), nullptr) };
                    if (!r)
                        return -1;
                    return truth(r.get());
                }
            // (an object whose class has the check (e.g. typing's aliases: typing.List))
            if (!is_class(aInfo) && is_instance(aInfo))
                if (ref const check{ special_method(aInfo, aCheck) })
                {
                    ref const args{ make_tuple({ (retain(aValue), aValue) }) };
                    ref const r{ call(check.get(), args.get(), nullptr) };
                    if (!r)
                        return -1;
                    return truth(r.get());
                }
            aClasses.push_back(aInfo);
            return 0;
        }

        object* b_issubclass(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "issubclass") || !arity(aArgs, 2u, 2u, "issubclass"))
                return nullptr;
            std::vector<object*> classes;
            if (auto const r = class_info(aArgs[1], aArgs[0], "__subclasscheck__", classes); r != 0)
                return r < 0 ? nullptr : make_bool(true);
            for (auto c : classes)
            {
                if (is_class(aArgs[0]) && is_class(c) && is_subclass(aArgs[0], c))
                    return make_bool(true);
                if (is_type_object(c) && as<type_object>(c)->name == "object")
                    return make_bool(true);
                if (is_class(aArgs[0]) && is_type_object(c) && !as<class_object>(aArgs[0])->builtinBase.empty() && 
                    as<type_object>(c)->qualifiedName == as<class_object>(aArgs[0])->builtinBase)
                    return make_bool(true);
                if (is_class(aArgs[0]) && is_type_object(c) && as<class_object>(aArgs[0])->nativeBase != nullptr &&
                    as<type_object>(c)->qualifiedName == as<type_object>(as<class_object>(aArgs[0])->nativeBase)->qualifiedName)
                    return make_bool(true);
                auto const name = is_class(aArgs[0]) ? as<class_object>(aArgs[0])->name : is_type_object(aArgs[0]) ? as<type_object>(aArgs[0])->qualifiedName : std::string{};
                if (is_type_object(c) && is_exception_class(as<type_object>(c)->qualifiedName) && is_exception_class(name) && is_exception_subclass(name, as<type_object>(c)->qualifiedName))
                    return make_bool(true);
                if (is_type_object(aArgs[0]) && is_type_object(c))
                {
                    // (the builtin types' bases)
                    static std::map<std::string, std::string, std::less<>> const sBases = { { "bool", "int" }, { "collections.OrderedDict", "dict" },
                        { "collections.defaultdict", "dict" }, { "collections.Counter", "dict" } };
                    for (std::string t = as<type_object>(aArgs[0])->qualifiedName;;)
                    {
                        if (t == as<type_object>(c)->qualifiedName)
                            return make_bool(true);
                        auto const base = sBases.find(t);
                        if (base == sBases.end())
                            break;
                        t = base->second;
                    }
                }
            }
            if (!is_class(aArgs[0]) && !is_type_object(aArgs[0]))
                return error("TypeError", "issubclass() arg 1 must be a class");
            return make_bool(false);
        }

        char const* instance_class_name(object* aObject)
        {
            return is_instance(aObject) ? as<class_object>(as<instance_object>(aObject)->cls)->name.c_str() : nullptr;
        }

        std::string const& as_class_name(object* aClass)
        {
            return as<class_object>(aClass)->name;
        }

        object* b_class(std::vector<object*> const& aArgs, object*)
        {
            if (aArgs.size() != 3u || kind_of(aArgs[0]) != NEOS_PY_STR || kind_of(aArgs[1]) != NEOS_PY_TUPLE || kind_of(aArgs[2]) != NEOS_PY_DICT)
                return error("TypeError", "__class(name, bases, namespace)");
            return fn_class(as<str_object>(aArgs[0])->value.c_str(), aArgs[1], aArgs[2]);
        }

        object* b_isinstance(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "isinstance") || !arity(aArgs, 2u, 2u, "isinstance"))
                return nullptr;
            std::vector<object*> classes;
            if (auto const r = class_info(aArgs[1], aArgs[0], "__instancecheck__", classes); r != 0)
                return r < 0 ? nullptr : make_bool(true);
            auto const userClass = user_class_of(aArgs[0]);
            for (auto c : classes)
                if (is_class(c) && userClass != nullptr && is_subclass(userClass, c))
                    return make_bool(true);
            // (a class: an instance of its metaclass, and of type)
            if (is_class(aArgs[0]) || is_type_object(aArgs[0]))
            {
                auto const metaclass = is_class(aArgs[0]) ? as<class_object>(aArgs[0])->metaclass : nullptr;
                for (auto c : classes)
                    if ((is_class(c) && metaclass != nullptr && is_subclass(metaclass, c)) ||
                        (is_type_object(c) && (as<type_object>(c)->qualifiedName == "type" || as<type_object>(c)->qualifiedName == "object")))
                        return make_bool(true);
            }
            for (auto c : classes)
                if (is_class(c) && as<class_object>(c)->instancecheck != nullptr && as<class_object>(c)->instancecheck(aArgs[0]) == 1)
                    return make_bool(true);
            if (userClass != nullptr && aArgs[0]->type != &sExceptionType.base)
            {
                auto const& base = as<class_object>(userClass)->builtinBase;
                auto const native = as<class_object>(userClass)->nativeBase;
                for (auto c : classes)
                    if (is_type_object(c) && (as<type_object>(c)->qualifiedName == "object" || (!base.empty() && as<type_object>(c)->qualifiedName == base) ||
                        (native != nullptr && as<type_object>(c)->qualifiedName == as<type_object>(native)->qualifiedName) ||
                        (as<type_object>(c)->instancecheck != nullptr && as<type_object>(c)->instancecheck(context(), as<type_object>(c)->data, aArgs[0]) == 1)))
                        return make_bool(true);
                return make_bool(false);
            }
            ref const actual{ class_of(aArgs[0]) };
            auto const& name = is_type_object(actual.get()) ? as<type_object>(actual.get())->qualifiedName : as_class_name(actual.get());
            for (auto c : classes)
            {
                if (is_class(c))
                    continue;
                if (!is_type_object(c))
                    return error("TypeError", "isinstance() arg 2 must be a type, a tuple of types, or a union");
                if (as<type_object>(c)->instancecheck != nullptr)
                {
                    if (as<type_object>(c)->instancecheck(context(), as<type_object>(c)->data, aArgs[0]) == 1)
                        return make_bool(true);
                    continue;
                }
                auto const& expected = as<type_object>(c)->qualifiedName;
                if (expected == name || expected == "object" || (expected == "int" && name == "bool"))
                    return make_bool(true);
            }
            return make_bool(false);
        }

        object* type_new(object* aMetatype, std::string const& aName, object* aBases, object* aNamespace, object* aKwargs);

        object* b_type(std::vector<object*> const& aArgs, object* aKwargs)
        {
            // (type(name, bases, dict): a class)
            if (aArgs.size() == 3u)
            {
                if (kind_of(aArgs[0]) != NEOS_PY_STR)
                    return error("TypeError", std::string{ "type.__new__() argument 1 must be str, not " } + type_name(aArgs[0]));
                if (kind_of(aArgs[2]) != NEOS_PY_DICT)
                    return error("TypeError", std::string{ "type.__new__() argument 3 must be dict, not " } + type_name(aArgs[2]));
                // (the base classes' metaclass)
                object* metaclass = nullptr;
                if (kind_of(aArgs[1]) == NEOS_PY_TUPLE)
                    for (auto b : as<tuple_object>(aArgs[1])->items)
                        if (is_class(b) && class_metaclass(b) != nullptr)
                            metaclass = class_metaclass(b);
                if (metaclass != nullptr)
                {
                    ref const args{ make_tuple({ (retain(aArgs[0]), aArgs[0]), (retain(aArgs[1]), aArgs[1]), (retain(aArgs[2]), aArgs[2]) }) };
                    return call(metaclass, args.get(), aKwargs);
                }
                return type_new(nullptr, as<str_object>(aArgs[0])->value, aArgs[1], aArgs[2], aKwargs);
            }
            if (!no_keywords(aKwargs, "type") || !arity(aArgs, 1u, 3u, "type"))
                return nullptr;
            if (aArgs.size() != 1u)
                return error("TypeError", "type() takes 1 or 3 arguments");
            return class_of(aArgs[0]);
        }

        object* b_round(std::vector<object*> const& aArgs, object* aKwargs);

        object* b_round_of(std::vector<object*> const& aArgs, object* aKwargs)
        {
            // (a subclass of a builtin number type: its value)
            if (!aArgs.empty() && builtin_value(aArgs[0]) != aArgs[0] && !has_special_method(aArgs[0], "__round__"))
            {
                auto args = aArgs;
                args[0] = builtin_value(aArgs[0]);
                return b_round(args, aKwargs);
            }
            return b_round(aArgs, aKwargs);
        }

        object* b_round(std::vector<object*> const& aArgs, object* aKwargs)
        {
            object* digits = nullptr;
            if (!keywords(aKwargs, { { "ndigits", &digits } }, "round") || !arity(aArgs, 1u, 2u, "round"))
                return nullptr;
            if (aArgs.size() > 1u)
                digits = aArgs[1];
            bool const none = digits == nullptr || kind_of(digits) == NEOS_PY_NONE;
            long long n = 0;
            if (!none && !to_index(digits, n))
                return nullptr;
            auto const k = kind_of(aArgs[0]);
            if (k == NEOS_PY_INT || k == NEOS_PY_BOOL)
            {
                if (none || n >= 0)
                    return k == NEOS_PY_INT ? (retain(aArgs[0]), aArgs[0]) : make_int(int_value(aArgs[0]));
                // (round half to even to a multiple of 10^-n)
                auto const value = big_value(aArgs[0]);
                if (-n > 2 * static_cast<long long>(big_bit_length(value)) + 2)
                    return make_int(0);
                big_integer p{ 1 };
                for (long long i = 0; i < -n; ++i)
                    p = p * big_integer{ 10 };
                big_integer q, r;
                big_divmod(value, p, q, r);
                auto const twice = r + r;
                if (p < twice || (twice == p && big_bit(big_abs(q), 0u)))
                    q = q + big_integer{ 1 };
                return make_int(q * p);
            }
            if (k == NEOS_PY_FLOAT)
            {
                auto const x = as<float_object>(aArgs[0])->value;
                if (none)
                {
                    if (!std::isfinite(x))
                        return error(std::isnan(x) ? "ValueError" : "OverflowError", std::isnan(x) ? "cannot convert float NaN to integer" : "cannot convert float infinity to integer");
                    auto const r = std::nearbyint(x); // (round half to even)
                    if (r >= 9223372036854775808.0 || r < -9223372036854775808.0)
                        return make_int(big_from_double(r));
                    return make_int(static_cast<long long>(r));
                }
                if (!std::isfinite(x) || x == 0.0)
                    return make_float(x);
                if (n > 323)
                    return make_float(x);
                if (n < -308)
                    return make_float(std::copysign(0.0, x));
                // (as CPython: the correctly rounded decimal string, then back to a double)
                char buffer[512];
                std::to_chars_result converted;
                if (n >= 0)
                    converted = std::to_chars(buffer, buffer + sizeof(buffer), x, std::chars_format::fixed, static_cast<int>(n));
                else
                {
                    double const scale = std::pow(10.0, static_cast<double>(-n));
                    auto const y = x / scale;
                    converted = std::to_chars(buffer, buffer + sizeof(buffer), std::nearbyint(y) * scale, std::chars_format::fixed, 0);
                }
                double result = 0.0;
                std::from_chars(buffer, converted.ptr, result);
                if (result == 0.0)
                    result = std::copysign(0.0, x);
                return make_float(result);
            }
            if (k == NEOS_PY_OTHER)
            {
                // (an object of another plugin: its __round__ is its unary operator "round")
                if (auto const type = python_type(aArgs[0]); type != nullptr && type->getattr != nullptr)
                {
                    ref const method{ type->getattr(context(), aArgs[0], "__round__") };
                    if (method)
                    {
                        ref const args{ none ? make_tuple() : make_tuple({ make_int(n) }) };
                        return call(method.get(), args.get(), nullptr);
                    }
                    return nullptr;
                }
            }
            return error("TypeError", std::string{ "type " } + type_name(aArgs[0]) + " doesn't define __round__ method");
        }

        object* b_divmod(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "divmod") || !arity(aArgs, 2u, 2u, "divmod"))
                return nullptr;
            // (ints beyond 64 bits: with one division)
            if (limb_view a, d; (is_big(aArgs[0]) || is_big(aArgs[1])) && limbs_of(aArgs[0], a) && limbs_of(aArgs[1], d) && d.size != 0u)
            {
                object* remainder = nullptr;
                ref q{ divide_views(a, d, false, &remainder) };
                ref r{ remainder };
                return make_tuple({ q.release_ownership(), r.release_ownership() });
            }
            ref q{ binary(NEOS_PY_FLOOR_DIVIDE, aArgs[0], aArgs[1], false) };
            if (!q)
                return nullptr;
            ref r{ binary(NEOS_PY_REMAINDER, aArgs[0], aArgs[1], false) };
            if (!r)
                return nullptr;
            return make_tuple({ q.release_ownership(), r.release_ownership() });
        }

        object* b_pow(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "pow") || !arity(aArgs, 2u, 3u, "pow"))
                return nullptr;
            if (aArgs.size() == 2u || kind_of(aArgs[2]) == NEOS_PY_NONE)
                return binary(NEOS_PY_POWER, aArgs[0], aArgs[1], false);
            long long b = 0, e = 0, m = 0;
            if (!is_int_like(aArgs[0]) || !is_int_like(aArgs[1]) || !is_int_like(aArgs[2]))
                return error("TypeError", "pow() 3rd argument not allowed unless all arguments are integers");
            if (is_big(aArgs[0]) || is_big(aArgs[1]) || is_big(aArgs[2]) || int_value(aArgs[1]) < 0)
            {
                // (an exponent that is not negative: with limbs)
                {
                    limb_view bv, ev, mv;
                    if (limbs_of(aArgs[0], bv) && limbs_of(aArgs[1], ev) && limbs_of(aArgs[2], mv) && (!ev.negative || ev.size == 0u) && mv.size != 0u)
                        return power_modulo_views(bv, ev, mv);
                }
                // (beyond 64 bits, or a negative exponent (the inverse of the base): with big integers)
                auto const floor_mod = [](big_integer const& a, big_integer const& n) { big_integer q, r; big_divmod(a, n, q, r); return r; };
                auto B = big_value(aArgs[0]);
                auto E = big_value(aArgs[1]);
                auto M = big_value(aArgs[2]);
                if (big_zero(M))
                    return error("ValueError", "pow() 3rd argument cannot be 0");
                bool const negativeModulus = big_negative(M);
                M = big_abs(M);
                if (big_negative(E))
                {
                    // (extended Euclid: the inverse of B modulo M)
                    big_integer r0 = floor_mod(B, M), r1 = M, s0{ 1 }, s1{ 0 };
                    while (!big_zero(r1))
                    {
                        big_integer q, r;
                        big_divmod(r0, r1, q, r);
                        r0 = r1; r1 = r;
                        auto const s2 = s0 - q * s1;
                        s0 = s1; s1 = s2;
                    }
                    if (r0 != big_integer{ 1 } && !(M == big_integer{ 1 }))
                        return error("ValueError", "base is not invertible for the given modulus");
                    B = s0;
                    E = -E;
                }
                big_integer result = floor_mod(big_integer{ 1 }, M);
                big_integer base = floor_mod(B, M);
                auto const bits = big_bit_length(E);
                for (std::uint64_t i = 0u; i < bits; ++i)
                {
                    if (big_bit(E, i))
                        result = floor_mod(result * base, M);
                    if (i + 1u < bits)
                        base = floor_mod(base * base, M);
                }
                if (negativeModulus && !big_zero(result))
                    result = result - M;
                return make_int(std::move(result));
            }
            b = int_value(aArgs[0]); e = int_value(aArgs[1]); m = int_value(aArgs[2]);
            if (m == 0)
                return error("ValueError", "pow() 3rd argument cannot be 0");
            auto const modulus = static_cast<unsigned long long>(m < 0 ? -m : m);
            // (a * b % modulus without overflow)
            auto const multiply_mod = [modulus](unsigned long long a, unsigned long long b)
            {
                unsigned long long result = 0u;
                a %= modulus;
                for (; b != 0u; b >>= 1)
                {
                    if ((b & 1u) != 0u)
                        result = (result >= modulus - a) ? result - (modulus - a) : result + a;
                    a = (a >= modulus - a) ? a - (modulus - a) : a + a;
                }
                return result;
            };
            long long bb = b % static_cast<long long>(modulus);
            if (bb < 0)
                bb += static_cast<long long>(modulus);
            unsigned long long result = 1u % modulus;
            unsigned long long base = static_cast<unsigned long long>(bb);
            for (; e > 0; e >>= 1)
            {
                if ((e & 1) != 0)
                    result = multiply_mod(result, base);
                base = multiply_mod(base, base);
            }
            auto r = static_cast<long long>(result);
            if (m < 0 && r != 0)
                r += m;
            return make_int(r);
        }

        object* b_chr(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "chr") || !arity(aArgs, 1u, 1u, "chr"))
                return nullptr;
            long long c = 0;
            if (!to_index(aArgs[0], c))
                return nullptr;
            if (c < 0 || c > 0x10FFFF)
                return error("ValueError", "chr() arg not in range(0x110000)");
            std::string text;
            auto const u = static_cast<unsigned long>(c);
            if (u < 0x80u) text += static_cast<char>(u);
            else if (u < 0x800u) { text += static_cast<char>(0xC0u | (u >> 6)); text += static_cast<char>(0x80u | (u & 0x3Fu)); }
            else if (u < 0x10000u) { text += static_cast<char>(0xE0u | (u >> 12)); text += static_cast<char>(0x80u | ((u >> 6) & 0x3Fu)); text += static_cast<char>(0x80u | (u & 0x3Fu)); }
            else { text += static_cast<char>(0xF0u | (u >> 18)); text += static_cast<char>(0x80u | ((u >> 12) & 0x3Fu)); text += static_cast<char>(0x80u | ((u >> 6) & 0x3Fu)); text += static_cast<char>(0x80u | (u & 0x3Fu)); }
            return make_str(std::move(text));
        }

        object* b_ord(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "ord") || !arity(aArgs, 1u, 1u, "ord"))
                return nullptr;
            if (kind_of(aArgs[0]) != NEOS_PY_STR)
                return error("TypeError", std::string{ "ord() expected string of length 1, but " } + type_name(aArgs[0]) + " found");
            auto const& text = as<str_object>(aArgs[0])->value;
            auto const n = length(aArgs[0]);
            if (n != 1)
                return error("TypeError", "ord() expected a character, but string of length " + std::to_string(n) + " found");
            auto const b = static_cast<unsigned char>(text[0]);
            unsigned long c = b;
            if (b >= 0xF0u) c = ((b & 0x07u) << 18) | ((text[1] & 0x3Fu) << 12) | ((text[2] & 0x3Fu) << 6) | (text[3] & 0x3Fu);
            else if (b >= 0xE0u) c = ((b & 0x0Fu) << 12) | ((text[1] & 0x3Fu) << 6) | (text[2] & 0x3Fu);
            else if (b >= 0xC0u) c = ((b & 0x1Fu) << 6) | (text[1] & 0x3Fu);
            return make_int(static_cast<long long>(c));
        }

        object* radix(std::vector<object*> const& aArgs, object* aKwargs, char const* aName, int aBase, char const* aPrefix)
        {
            if (!no_keywords(aKwargs, aName) || !arity(aArgs, 1u, 1u, aName))
                return nullptr;
            long long v = 0;
            if (is_big(aArgs[0]))
            {
                std::string digits;
                bool negative = false;
                if (int_digits(aArgs[0], aBase, digits, negative))
                    return make_str(std::string{ negative ? "-" : "" } + aPrefix + digits);
                return make_str(std::string{ big_negative(big_of(aArgs[0])) ? "-" : "" } + aPrefix + big_digits(big_of(aArgs[0]), aBase));
            }
            if (!to_index(aArgs[0], v))
                return nullptr;
            unsigned long long u = v < 0 ? 0ull - static_cast<unsigned long long>(v) : static_cast<unsigned long long>(v);
            char buffer[80];
            auto const converted = std::to_chars(buffer, buffer + sizeof(buffer), u, aBase);
            return make_str(std::string{ v < 0 ? "-" : "" } + aPrefix + std::string{ buffer, converted.ptr });
        }

        object* b_hex(std::vector<object*> const& aArgs, object* aKwargs) { return radix(aArgs, aKwargs, "hex", 16, "0x"); }
        object* b_oct(std::vector<object*> const& aArgs, object* aKwargs) { return radix(aArgs, aKwargs, "oct", 8, "0o"); }
        object* b_bin(std::vector<object*> const& aArgs, object* aKwargs) { return radix(aArgs, aKwargs, "bin", 2, "0b"); }

        // (the exception raised is an AttributeError: it is handled (true); otherwise it remains raised (false))
        bool attribute_error_caught()
        {
            if (std::string_view const text{ error_text() }; text == "AttributeError" || text.starts_with("AttributeError: "))
            {
                clear_error();
                return true;
            }
            ref const e{ take_exception() };
            if (exception_is(e.get(), "AttributeError"))
                return true;
            reraise_exception(e.get());
            return false;
        }

        object* b_getattr(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "getattr") || !arity(aArgs, 2u, 3u, "getattr"))
                return nullptr;
            if (kind_of(aArgs[1]) != NEOS_PY_STR)
                return error("TypeError", "attribute name must be string, not '" + std::string{ type_name(aArgs[1]) } + "'");
            auto const result = getattr(aArgs[0], as<str_object>(aArgs[1])->value.c_str());
            if (result == nullptr)
                note_attribute_error(aArgs[0]);
            if (result == nullptr && aArgs.size() == 3u && attribute_error_caught())
            {
                retain(aArgs[2]);
                return aArgs[2];
            }
            return result;
        }

        object* b_hasattr(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "hasattr") || !arity(aArgs, 2u, 2u, "hasattr"))
                return nullptr;
            if (kind_of(aArgs[1]) != NEOS_PY_STR)
                return error("TypeError", "attribute name must be string, not '" + std::string{ type_name(aArgs[1]) } + "'");
            ref const result{ getattr(aArgs[0], as<str_object>(aArgs[1])->value.c_str()) };
            if (!result && !attribute_error_caught())
                return nullptr;
            return make_bool(static_cast<bool>(result));
        }

        object* b_setattr(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "setattr") || !arity(aArgs, 3u, 3u, "setattr"))
                return nullptr;
            if (kind_of(aArgs[1]) != NEOS_PY_STR)
                return error("TypeError", "attribute name must be string, not '" + std::string{ type_name(aArgs[1]) } + "'");
            return setattr(aArgs[0], as<str_object>(aArgs[1])->value.c_str(), aArgs[2]) < 0 ? nullptr : none();
        }

        object* b_delattr(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "delattr") || !arity(aArgs, 2u, 2u, "delattr"))
                return nullptr;
            if (kind_of(aArgs[1]) != NEOS_PY_STR)
                return error("TypeError", std::string{ "attribute name must be string, not '" } + type_name(aArgs[1]) + "'");
            return setattr(aArgs[0], as<str_object>(aArgs[1])->value.c_str(), nullptr) < 0 ? nullptr : none();
        }

        object* b_vars(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "vars") || !arity(aArgs, 1u, 1u, "vars"))
                return nullptr;
            auto const result = getattr(aArgs[0], "__dict__");
            if (result == nullptr)
            {
                clear_error();
                return error("TypeError", "vars() argument must have __dict__ attribute");
            }
            return result;
        }

        object* b_dir(std::vector<object*> const& aArgs, object* aKwargs);

        object* b_ascii(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "ascii") || !arity(aArgs, 1u, 1u, "ascii"))
                return nullptr;
            std::string text;
            if (!repr_of(aArgs[0], text))
                return nullptr;
            std::string result;
            for (auto c : to_code_points(text))
            {
                char buffer[16];
                if (c < 0x80u)
                    result += static_cast<char>(c);
                else
                {
                    std::snprintf(buffer, sizeof(buffer), c <= 0xFFu ? "\\x%02x" : c <= 0xFFFFu ? "\\u%04x" : "\\U%08x", static_cast<unsigned>(c));
                    result += buffer;
                }
            }
            return make_str(result);
        }

        object* b_callable(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "callable") || !arity(aArgs, 1u, 1u, "callable"))
                return nullptr;
            auto const k = kind_of(aArgs[0]);
            auto const type = python_type(aArgs[0]);
            if (is_instance(aArgs[0]))
                return make_bool(has_special_method(aArgs[0], "__call__"));
            return make_bool(k == NEOS_PY_FUNCTION || (k == NEOS_PY_OTHER && type != nullptr && type->call != nullptr));
        }

        object* b_map(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "map"))
                return nullptr;
            if (aArgs.size() < 2u)
                return error("TypeError", "map() must have at least two arguments.");
            return lazy_iterator(sMapType, std::vector<object*>{ aArgs.begin() + 1, aArgs.end() }, aArgs[0]);
        }

        object* b_filter(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "filter") || !arity(aArgs, 2u, 2u, "filter"))
                return nullptr;
            return lazy_iterator(sFilterType, { aArgs[1] }, aArgs[0]);
        }

        object* b_input(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "input") || !arity(aArgs, 0u, 1u, "input"))
                return nullptr;
            if (!aArgs.empty())
            {
                std::string prompt;
                if (!str_of(aArgs[0], prompt))
                    return nullptr;
                if (!write_out(prompt))
                    return nullptr;
            }
            std::string line;
            if (!read_standard_line(line))
                return error("EOFError", "EOF when reading a line");
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
                line.pop_back();
            return make_str(std::move(line));
        }

        object* b_slice(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "slice") || !arity(aArgs, 1u, 3u, "slice"))
                return nullptr;
            if (aArgs.size() == 1u)
                return make_slice(nullptr, aArgs[0], nullptr);
            return make_slice(aArgs[0], aArgs[1], aArgs.size() > 2u ? aArgs[2] : nullptr);
        }

        object* b_object(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!aArgs.empty() || (aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty()))
                return error("TypeError", "object() takes no arguments");
            return make_plain_object();
        }

        object* b_format(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "format") || !arity(aArgs, 1u, 2u, "format"))
                return nullptr;
            std::string spec;
            if (aArgs.size() == 2u)
            {
                if (kind_of(aArgs[1]) != NEOS_PY_STR)
                    return error("TypeError", std::string{ "format() argument 2 must be str, not " } + type_name(aArgs[1]));
                spec = as<str_object>(aArgs[1])->value;
            }
            std::string text;
            return format_value(aArgs[0], spec, text) ? make_str(std::move(text)) : nullptr;
        }

        object* b_hash(std::vector<object*> const& aArgs, object* aKwargs);
        object* b_id(std::vector<object*> const& aArgs, object* aKwargs);
        object* b_ns_bind(std::vector<object*> const& aArgs, object* aKwargs);

        builtin_entry const sBuiltinFunctions[] =
        {
            { "abs", b_abs }, { "all", b_all }, { "any", b_any }, { "bin", b_bin }, { "callable", b_callable }, { "chr", b_chr },
            { "divmod", b_divmod }, { "enumerate", b_enumerate }, { "filter", b_filter }, { "format", b_format }, { "getattr", b_getattr }, { "hasattr", b_hasattr }, 
            { "hex", b_hex }, { "input", b_input }, { "isinstance", b_isinstance }, { "iter", b_iter }, { "len", b_len }, { "map", b_map },
            { "max", b_max }, { "min", b_min }, { "next", b_next }, { "oct", b_oct }, { "ord", b_ord }, { "pow", b_pow }, { "print", b_print },
            { "repr", b_repr }, { "reversed", b_reversed }, { "round", b_round_of }, { "setattr", b_setattr }, { "delattr", b_delattr }, { "vars", b_vars }, { "dir", b_dir }, { "ascii", b_ascii }, { "sorted", b_sorted }, { "sum", b_sum },
            { "zip", b_zip }, { "__class", b_class }, { "__build_class", b_build_class }, { "__ns_bind", b_ns_bind },
            { "super", b_super }, { "issubclass", b_issubclass }, { "hash", b_hash }, { "id", b_id }, { "__bytes", b_bytes_literal }, { "__cell", b_cell },
            { "__closure", b_closure }, { "__generator", b_generator }, { "open", b_open }, { "__yield", b_yield }, { "__yield_from", b_yield_from },
            { "__with_items", b_with_items }, { "__match", b_match },
            { "__coroutine", b_coroutine }, { "__async_generator", b_async_generator }, { "__await", b_await }, { "__async_iter", b_async_iter },
            { "__async_with", b_async_with }, { "__annotate", b_annotate }, { "__type_param", b_type_param }, { "__type_alias", b_type_alias }, { "__generic_base", b_generic_base }, { "__generic_function", b_generic_function }, { "__exception_group", b_exception_group }, { "exec", b_exec }, { "eval", b_eval }, { "__import__", b_import }, { "compile", b_compile }, { "__exec_result", b_exec_result }
        };

        // (the builtin classes)
        builtin_entry const sBuiltinClasses[] =
        {
            { "int", b_int }, { "float", b_float }, { "str", b_str }, { "bool", b_bool }, { "complex", b_complex }, { "list", b_list },
            { "tuple", b_tuple }, { "dict", b_dict }, { "range", b_range }, { "type", b_type }, { "slice", b_slice }, { "object", b_object },
            { "set", b_set }, { "frozenset", b_frozenset }, { "bytes", b_bytes }, { "bytearray", b_bytearray },
            { "memoryview", b_memoryview }, { "staticmethod", b_staticmethod }, { "classmethod", b_classmethod }, { "property", b_property }
        };

        // (the functions of the builtin classes (e.g. bytes.fromhex): "class.name")
        builtin_entry const sClassFunctions[] =
        {
            { "bytes.fromhex", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { std::vector<object*> a; return parse_arguments("bytes.fromhex", aArgs, aKwargs, { "string" }, 1u, 1u, a) ? bytes_fromhex(a[0], false) : nullptr; } },
            { "bytearray.fromhex", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { std::vector<object*> a; return parse_arguments("bytearray.fromhex", aArgs, aKwargs, { "string" }, 1u, 1u, a) ? bytes_fromhex(a[0], true) : nullptr; } }
        };

        // (object.__getattribute__(self, name), object.__setattr__(self, name, value) and object.__delattr__(self, name))
        object* fn_object_getattribute(neos_py_context*, void*, object* aArgs, object*)
        {
            auto const& args = tuple_items(aArgs);
            if (args.size() != 2u || kind_of(args[1]) != NEOS_PY_STR)
                return error("TypeError", "expected 2 arguments");
            auto const self = args[0];
            auto const& name = as<str_object>(args[1])->value;
            if (is_instance(self))
            {
                if (auto const r = user_getattr_default(self, as<instance_object>(self)->attributes, name.c_str()))
                    return r;
                if (failed())
                    return nullptr;
                return error("AttributeError", "'" + as<class_object>(as<instance_object>(self)->cls)->name + "' object has no attribute '" + name + "'");
            }
            return getattr(self, name.c_str());
        }
        object* fn_object_setattr(neos_py_context*, void* aData, object* aArgs, object*)
        {
            auto const& args = tuple_items(aArgs);
            bool const del = aData != nullptr;
            if (args.size() != (del ? 2u : 3u) || kind_of(args[1]) != NEOS_PY_STR)
                return error("TypeError", del ? "expected 2 arguments" : "expected 3 arguments");
            auto const self = args[0];
            auto const& name = as<str_object>(args[1])->value;
            int const r = is_instance(self) ? user_setattr_default(self, as<instance_object>(self)->attributes, name.c_str(), del ? nullptr : args[2]) :
                is_class(self) ? class_setattr_raw(self, name.c_str(), del ? nullptr : args[2]) : setattr(self, name.c_str(), del ? nullptr : args[2]);
            return r < 0 ? nullptr : (retain(none()), none());
        }

        long long pointer_hash(void const* aPointer);

        extern thread_local bool tObjectDir;    // (object.__dir__ is running: dir() does not call the class's __dir__)

        // (the methods of object (e.g. object.__eq__): self is the first argument)
        builtin_entry const sObjectMethods[] =
        {
            { "__eq__", [](std::vector<object*> const& aArgs, object*) -> object*
                {
                    if (aArgs.size() != 2u)
                        return error("TypeError", "expected 1 argument, got " + std::to_string(aArgs.empty() ? 0u : aArgs.size() - 1u));
                    return aArgs[0] == aArgs[1] ? make_bool(true) : not_implemented();
                } },
            { "__ne__", [](std::vector<object*> const& aArgs, object*) -> object*
                {
                    if (aArgs.size() != 2u)
                        return error("TypeError", "expected 1 argument, got " + std::to_string(aArgs.empty() ? 0u : aArgs.size() - 1u));
                    // (the inverse of __eq__)
                    ref eq;
                    if (auto const method = special_method(aArgs[0], "__eq__"))
                    {
                        ref const m{ method };
                        ref const args{ make_tuple({ (retain(aArgs[1]), aArgs[1]) }) };
                        eq = ref{ call(m.get(), args.get(), nullptr) };
                    }
                    else
                        eq = ref{ aArgs[0] == aArgs[1] ? make_bool(true) : not_implemented() };
                    if (!eq || kind_of(eq.get()) == NEOS_PY_NOT_IMPLEMENTED)
                        return eq.release_ownership();
                    auto const t = truth(eq.get());
                    return t < 0 ? nullptr : make_bool(t == 0);
                } },
            { "__lt__", [](std::vector<object*> const&, object*) -> object* { return not_implemented(); } },
            { "__le__", [](std::vector<object*> const&, object*) -> object* { return not_implemented(); } },
            { "__gt__", [](std::vector<object*> const&, object*) -> object* { return not_implemented(); } },
            { "__ge__", [](std::vector<object*> const&, object*) -> object* { return not_implemented(); } },
            { "__hash__", [](std::vector<object*> const& aArgs, object*) -> object*
                {
                    if (aArgs.size() != 1u)
                        return error("TypeError", "expected 0 arguments");
                    return make_int(pointer_hash(aArgs[0]));
                } },
            { "__repr__", [](std::vector<object*> const& aArgs, object*) -> object*
                {
                    if (aArgs.size() != 1u)
                        return error("TypeError", "expected 0 arguments");
                    if (auto const cls = user_class_of(aArgs[0]); cls != nullptr && is_instance(aArgs[0]))
                        return make_str("<" + as<class_object>(cls)->module + "." + as<class_object>(cls)->name + " object at " + object_address(aArgs[0]) + ">");
                    std::string text;
                    return repr_of(aArgs[0], text) ? make_str(text) : nullptr;
                } },
            { "__str__", [](std::vector<object*> const& aArgs, object*) -> object*
                {
                    if (aArgs.size() != 1u)
                        return error("TypeError", "expected 0 arguments");
                    std::string text;
                    return repr_of(aArgs[0], text) ? make_str(text) : nullptr;
                } },
            { "__format__", [](std::vector<object*> const& aArgs, object*) -> object*
                {
                    if (aArgs.size() != 2u || kind_of(aArgs[1]) != NEOS_PY_STR)
                        return error("TypeError", "__format__() argument must be str");
                    if (!as<str_object>(aArgs[1])->value.empty())
                        return error("TypeError", std::string{ "unsupported format string passed to " } + type_name(aArgs[0]) + ".__format__");
                    std::string text;
                    return str_of(aArgs[0], text) ? make_str(text) : nullptr;
                } },
            { "__sizeof__", [](std::vector<object*> const&, object*) -> object* { return make_int(16); } },
            { "__dir__", [](std::vector<object*> const& aArgs, object*) -> object*
                {
                    if (aArgs.size() != 1u)
                        return error("TypeError", "expected 0 arguments");
                    // (object.__dir__: the names, without the class's __dir__)
                    tObjectDir = true;
                    auto const result = b_dir({ aArgs[0] }, nullptr);
                    tObjectDir = false;
                    return result;
                } },
            { "__getstate__", [](std::vector<object*> const& aArgs, object*) -> object*
                {
                    if (aArgs.size() != 1u)
                        return error("TypeError", "expected 0 arguments");
                    if (is_instance(aArgs[0]))
                    {
                        auto& instance = *as<instance_object>(aArgs[0]);
                        auto const& slots = as<class_object>(instance.cls)->slots;
                        object* dict = instance.attributes.dict != nullptr && !as<dict_object>(instance.attributes.dict)->items.empty() ? instance.attributes.dict : nullptr;
                        if (slots)
                        {
                            // ((None or its __dict__, the slots' values))
                            ref const values{ make_dict() };
                            if (dict != nullptr)
                                for (auto const& item : as<dict_object>(dict)->items)
                                    if (kind_of(item.first) == NEOS_PY_STR && std::find(slots->begin(), slots->end(), as<str_object>(item.first)->value) != slots->end())
                                        dict_set(values.get(), item.first, item.second);
                            if (as<dict_object>(values.get())->items.empty())
                                return new_reference(none());
                            return make_tuple({ new_reference(none()), new_reference(values.get()) });
                        }
                        return new_reference(dict != nullptr ? dict : none());
                    }
                    return new_reference(none());
                } },
            { "__reduce_ex__", [](std::vector<object*> const& aArgs, object*) -> object*
                {
                    if (aArgs.size() != 2u)
                        return error("TypeError", "__reduce_ex__() takes exactly one argument");
                    long long protocol = 0;
                    if (!to_index(aArgs[1], protocol))
                        return nullptr;
                    // (a class's own __reduce__)
                    if (auto const cls = user_class_of(aArgs[0]))
                        if (auto const reduce = class_lookup(cls, "__reduce__"))
                        {
                            ref const method{ bind_attribute(reduce, aArgs[0], cls) };
                            return invoke(method.get(), {});
                        }
                    return invoke(ref{ module_attribute("copyreg", "__reduce_ex__") }.get(), { new_reference(aArgs[0]), make_int(protocol) });
                } },
            { "__reduce__", [](std::vector<object*> const& aArgs, object*) -> object*
                {
                    if (aArgs.size() != 1u)
                        return error("TypeError", "expected 0 arguments");
                    return invoke(ref{ module_attribute("copyreg", "__reduce_ex__") }.get(), { new_reference(aArgs[0]), make_int(0) });
                } }
        };

        // (an attribute of object (e.g. its __eq__) of an object (bound) or of a class (aSelf null: unbound): null if none (no exception))
        object* object_attribute(object* aSelf, std::string_view aName)
        {
            for (auto const& m : sObjectMethods)
                if (aName == m.name)
                {
                    if (aSelf != nullptr)
                        return make_function(m.name, call_builtin, const_cast<builtin_entry*>(&m), aSelf);
                    // (unbound: one object, so that A.__eq__ is object.__eq__)
                    static std::map<builtin_entry const*, object*> sUnbound;
                    auto& f = sUnbound[&m];
                    if (f == nullptr)
                        f = make_function(m.name, call_builtin, const_cast<builtin_entry*>(&m));
                    return new_reference(f);
                }
            if (aName == "__subclasshook__")
                return make_function("__subclasshook__", [](neos_py_context*, void*, object*, object*) -> object* { return not_implemented(); }, nullptr);
            if (aSelf != nullptr && (aName == "__doc__" || aName == "__weakref__"))
                return new_reference(none());
            return nullptr;
        }

        object* class_function_of(std::string const& aClass, char const* aName);

        std::map<std::string, neos_py_type const*, std::less<>>& observed_types()
        {
            static std::map<std::string, neos_py_type const*, std::less<>> sTypes;
            return sTypes;
        }

        // (a type of a plugin (e.g. collections.deque): its special methods (unbound: they call the instance's), from the slots of its instances'
        // type (it is known once one of them has been seen))
        object* slot_function(std::string const& aClass, char const* aName)
        {
            auto const observed = observed_types().find(aClass);
            if (observed == observed_types().end())
                return nullptr;
            auto const& t = *observed->second;
            std::string_view const name{ aName };
            bool const has =
                (name == "__repr__" && t.repr != nullptr) || (name == "__str__" && (t.str != nullptr || t.repr != nullptr)) || (name == "__call__" && t.call != nullptr) ||
                (name == "__getitem__" && t.getitem != nullptr) || ((name == "__setitem__" || name == "__delitem__") && t.setitem != nullptr) ||
                (name == "__len__" && t.length != nullptr) || (name == "__iter__" && t.iter != nullptr) || (name == "__next__" && t.next != nullptr) ||
                (name == "__contains__" && t.contains != nullptr) || (name == "__hash__" && t.hash != nullptr) || (name == "__bool__" && t.truth != nullptr) ||
                ((name == "__eq__" || name == "__ne__" || name == "__lt__" || name == "__le__" || name == "__gt__" || name == "__ge__") && t.compare != nullptr);
            if (!has)
                return nullptr;
            static std::map<std::string, std::string> sNames;
            auto& stored = sNames.try_emplace(std::string{ aName }, aName).first->second;
            return make_function(aName, [](neos_py_context*, void* aData, object* aArgs, object* aKwargs) -> object*
            {
                auto const& args = tuple_items(aArgs);
                auto const& n = *static_cast<std::string const*>(aData);
                if (args.empty())
                    return error("TypeError", "descriptor '" + n + "' needs an argument");
                if (aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty() && n != "__call__")
                    return error("TypeError", n + "() takes no keyword arguments");
                // (the slot of the instance's value (an instance of a class that derives from the type: its value of it))
                auto const self = builtin_value(args[0]);
                auto const arity = [&](std::size_t aCount) { return args.size() == aCount + 1u ? true : (error("TypeError", "expected " + std::to_string(aCount) + 
                    " argument" + (aCount == 1u ? "" : "s") + ", got " + std::to_string(args.size() - 1u)), false); };
                if (n == "__repr__" || n == "__str__")
                {
                    std::string text;
                    if (!arity(0u) || !(n == "__repr__" ? repr_of(self, text) : str_of(self, text)))
                        return nullptr;
                    return make_str(text);
                }
                if (n == "__len__")
                {
                    if (!arity(0u))
                        return nullptr;
                    auto const l = length(self);
                    return l < 0 ? nullptr : make_int(l);
                }
                if (n == "__iter__")
                    return arity(0u) ? iter(self) : nullptr;
                if (n == "__next__")
                {
                    if (!arity(0u))
                        return nullptr;
                    auto const r = next(self);
                    return r != nullptr || failed() ? r : error("StopIteration", "");
                }
                if (n == "__contains__")
                {
                    if (!arity(1u))
                        return nullptr;
                    auto const c = contains(self, args[1]);
                    return c < 0 ? nullptr : make_bool(c == 1);
                }
                if (n == "__getitem__")
                    return arity(1u) ? getitem(self, args[1]) : nullptr;
                if (n == "__setitem__" || n == "__delitem__")
                {
                    if (!arity(n == "__setitem__" ? 2u : 1u) || setitem(self, args[1], n == "__setitem__" ? args[2] : nullptr) < 0)
                        return nullptr;
                    return new_reference(none());
                }
                if (n == "__hash__")
                {
                    long long h = 0;
                    if (!arity(0u) || !hash_of(self, h))
                        return nullptr;
                    return make_int(h);
                }
                if (n == "__bool__")
                {
                    if (!arity(0u))
                        return nullptr;
                    auto const t = truth(self);
                    return t < 0 ? nullptr : make_bool(t == 1);
                }
                if (n == "__call__")
                {
                    std::vector<object*> rest;
                    for (std::size_t i = 1u; i < args.size(); ++i)
                        rest.push_back(new_reference(args[i]));
                    ref const tuple{ make_tuple(std::move(rest)) };
                    return call(self, tuple.get(), aKwargs);
                }
                static std::pair<char const*, int> const sComparisons[] = { { "__eq__", NEOS_PY_EQ }, { "__ne__", NEOS_PY_NE }, { "__lt__", NEOS_PY_LT },
                    { "__le__", NEOS_PY_LE }, { "__gt__", NEOS_PY_GT }, { "__ge__", NEOS_PY_GE } };
                for (auto const& [name, op] : sComparisons)
                    if (n == name)
                        return arity(1u) ? compare(op, self, builtin_value(args[1])) : nullptr;
                return error("SystemError", "slot " + n);
            }, &stored);
        }

        // (a builtin type's function (e.g. dict.__repr__): the same object each time (list.__repr__ is list.__repr__))
        object* class_function(std::string const& aClass, char const* aName)
        {
            static std::map<std::pair<std::string, std::string>, object*> sFunctions;
            std::pair<std::string, std::string> key{ aClass, aName };
            if (auto const existing = sFunctions.find(key); existing != sFunctions.end())
                return new_reference(existing->second);
            auto const result = class_function_of(aClass, aName);
            if (result != nullptr)
                sFunctions.emplace(std::move(key), new_reference(result));
            return result;
        }

        namespace math_module
        {
            object* int_from_double(double x);
        }

        // (int's and float's rounding methods (of self, or unbound if it is null): __round__(self[, ndigits]), __floor__, __ceil__ and __trunc__)
        object* number_rounding(neos_py_context*, void* aData, object* aArgs, object*)
        {
            auto const name = static_cast<char const*>(aData);
            auto const& args = tuple_items(aArgs);
            bool const round = std::strcmp(name, "__round__") == 0;
            if (args.empty() || args.size() > (round ? 2u : 1u))
                return error("TypeError", std::string{ name } + "() takes " + (round ? "at most 1 argument" : "no arguments") + " (" +
                    std::to_string(args.empty() ? 0u : args.size() - 1u) + " given)");
            auto const x = builtin_value(args[0]);
            auto const k = kind_of(x);
            if (k != NEOS_PY_INT && k != NEOS_PY_BOOL && k != NEOS_PY_FLOAT)
                return error("TypeError", std::string{ "descriptor '" } + name + "' requires a number, not '" + type_name(args[0]) + "'");
            if (round)
                return b_round(args.size() == 2u ? std::vector<object*>{ x, args[1] } : std::vector<object*>{ x }, nullptr);
            if (k != NEOS_PY_FLOAT)
                return k == NEOS_PY_INT ? (retain(x), x) : make_int(int_value(x));
            auto const d = as<float_object>(x)->value;
            return math_module::int_from_double(name[2] == 'f' ? std::floor(d) : name[2] == 'c' ? std::ceil(d) : std::trunc(d));
        }

        object* number_rounding_method(char const* aName, object* aSelf)
        {
            static char const* const sRounding[] = { "__round__", "__floor__", "__ceil__", "__trunc__" };
            for (auto const& r : sRounding)
                if (std::strcmp(aName, r) == 0)
                    return make_function(aName, number_rounding, const_cast<char*>(r), aSelf);
            return nullptr;
        }


        object* class_function_of(std::string const& aClass, char const* aName)
        {
            auto const qualified = aClass + "." + aName;
            if (aClass == "object")
            {
                if (auto const method = object_attribute(nullptr, aName))
                    return method;
                static int sDelete = 1;
                if (std::strcmp(aName, "__getattribute__") == 0)
                    return make_function(aName, fn_object_getattribute, nullptr);
                if (std::strcmp(aName, "__setattr__") == 0)
                    return make_function(aName, fn_object_setattr, nullptr);
                if (std::strcmp(aName, "__delattr__") == 0)
                    return make_function(aName, fn_object_setattr, &sDelete);
                if (std::strcmp(aName, "__init_subclass__") == 0)
                    return make_function(aName, fn_object_init_subclass, nullptr);
            }
            if (aClass == "type")
            {
                if (std::strcmp(aName, "__new__") == 0)
                    return make_function(aName, fn_type_new, nullptr);
                if (std::strcmp(aName, "__call__") == 0)
                    return make_function(aName, fn_type_call, nullptr);
            }
            // (the descriptors' methods (e.g. property.__get__(p, obj, type)): as attribute lookup and assignment use them)
            if (std::strcmp(aName, "__get__") == 0 && (aClass == "property" || aClass == "staticmethod" || aClass == "classmethod" ||
                aClass == "functools.cached_property" || aClass == "function"))
                return make_function(aName, [](neos_py_context*, void*, object* aArgs, object*) -> object*
                {
                    auto const& args = tuple_items(aArgs);
                    if (args.size() < 2u || args.size() > 3u)
                        return error("TypeError", "expected 1 or 2 arguments, got " + std::to_string(args.empty() ? 0u : args.size() - 1u));
                    auto const instance = kind_of(args[1]) == NEOS_PY_NONE ? nullptr : args[1];
                    ref const owner{ args.size() == 3u && kind_of(args[2]) != NEOS_PY_NONE ? new_reference(args[2]) : instance != nullptr ? class_of(instance) : nullptr };
                    if (!owner)
                        return error("TypeError", "__get__(None, None) is invalid");
                    return bind_attribute(args[0], instance, owner.get());
                }, nullptr);
            if (aClass == "property" && (std::strcmp(aName, "__set__") == 0 || std::strcmp(aName, "__delete__") == 0))
            {
                static int sDeleter = 1;
                return make_function(aName, [](neos_py_context*, void* aData, object* aArgs, object*) -> object*
                {
                    bool const deleter = aData != nullptr;
                    auto const& args = tuple_items(aArgs);
                    if (args.size() != (deleter ? 2u : 3u) || args[0]->type != &sWrapperType.base)
                        return error("TypeError", std::string{ "descriptor '" } + (deleter ? "__delete__" : "__set__") + "' requires a 'property' object and " +
                            (deleter ? "1 argument" : "2 arguments"));
                    auto const& p = *as<wrapper_object>(args[0]);
                    auto const function = deleter ? p.deleter : p.setter;
                    if (function == nullptr)
                        return error("AttributeError", std::string{ "property object has no " } + (deleter ? "deleter" : "setter"));
                    ref const callArgs{ deleter ? make_tuple({ new_reference(args[1]) }) : make_tuple({ new_reference(args[1]), new_reference(args[2]) }) };
                    ref const r{ call(function, callArgs.get(), nullptr) };
                    return r ? new_reference(none()) : nullptr;
                }, std::strcmp(aName, "__delete__") == 0 ? &sDeleter : nullptr);
            }
            for (auto const& entry : sClassFunctions)
                if (qualified == entry.name)
                    return make_function(aName, call_builtin, const_cast<builtin_entry*>(&entry));
            if (std::strcmp(aName, "__new__") == 0 && (is_builtin_base(aClass) || aClass == "object"))
                return shared_function("__new__", builtin_new);
            if (std::strcmp(aName, "__init__") == 0 && (is_builtin_base(aClass) || aClass == "object"))
                return shared_function("__init__", builtin_init);
            if ((aClass == "int" || aClass == "bool") && std::strcmp(aName, "from_bytes") == 0)
                return make_function("from_bytes", int_from_bytes, nullptr);
            if (aClass == "itertools.chain" && std::strcmp(aName, "from_iterable") == 0)
                return itertools_chain_from_iterable();
            if (aClass == "fractions.Fraction")
                return fraction_class_function(aName);
            if (aClass == "decimal.Decimal")
                return decimal_class_function(aName);
            if (aClass.starts_with("datetime."))
                return datetime_class_function(aClass, aName);
            if (aClass == "str")
                if (auto const method = str_method(nullptr, aName))
                    return method;
            // (int's and float's rounding methods: __round__(self[, ndigits]), __floor__, __ceil__ and __trunc__ (of self))
            if (aClass == "int" || aClass == "bool" || aClass == "float")
                if (auto const method = number_rounding_method(aName, nullptr))
                    return method;
            if (is_builtin_base(aClass))
                if (auto const method = protocol_method(nullptr, -1, aName))
                    return method;
            // (the methods of the builtin types, unbound (e.g. list.append))
            static std::pair<char const*, int> const sKinds[] = { { "list", NEOS_PY_LIST }, { "tuple", NEOS_PY_TUPLE }, { "dict", NEOS_PY_DICT }, 
                { "int", NEOS_PY_INT }, { "float", NEOS_PY_FLOAT } };
            for (auto const& kind : sKinds)
                if (aClass == kind.first)
                {
                    if (auto const method = container_method(nullptr, kind.second, aName))
                        return method;
                    for (auto const& method : sMethods)
                        if (method.kind == kind.second && std::strcmp(method.name, aName) == 0)
                            return make_function(method.name, call_method, const_cast<method_entry*>(&method));
                }
            return slot_function(aClass, aName);
        }

        builtin_entry const* find_builtin(std::string_view aName)
        {
            for (auto const& entry : sBuiltinFunctions)
                if (aName == entry.name)
                    return &entry;
            for (auto const& entry : sBuiltinClasses)
                if (aName == entry.name)
                    return &entry;
            return nullptr;
        }

        // --- hash() and id() (CPython's hash values: numbers, str, tuples, range, None; an object without a value's hash: its identity)

        constexpr int kHashBits = 61;
        constexpr std::uint64_t kHashModulus = (std::uint64_t{ 1 } << kHashBits) - 1u;
        constexpr long long kHashInf = 314159;
        constexpr std::uint64_t kHashImag = 1000003u;

        long long pointer_hash(void const* aPointer)
        {
            // (_Py_HashPointer: the address rotated by 4 bits)
            auto const y = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(aPointer));
            auto const x = static_cast<long long>((y >> 4) | (y << 60));
            return x == -1 ? -2 : x;
        }

        long long finished_hash(std::uint64_t aHash)
        {
            return aHash == static_cast<std::uint64_t>(-1) ? -2 : static_cast<long long>(aHash);
        }

        long long int_hash(long long aValue)
        {
            // (n mod (2**61 - 1), with n's sign)
            if (aValue == std::numeric_limits<long long>::min())
                return finished_hash(static_cast<std::uint64_t>(-static_cast<long long>((std::uint64_t{ 1 } << 63) % kHashModulus)));
            auto const magnitude = static_cast<std::uint64_t>(aValue < 0 ? -aValue : aValue) % kHashModulus;
            return finished_hash(aValue < 0 ? static_cast<std::uint64_t>(-static_cast<long long>(magnitude)) : magnitude);
        }

        // (an int's magnitude mod 2**61 - 1 from its limbs (most significant first: x * 2**64 + limb, and 2**64 is 2**3 mod 2**61 - 1), with 
        // its sign)
        long long limbs_hash(limb_view const& aValue)
        {
            std::uint64_t x = 0u;
            for (auto i = aValue.size; i-- > 0u;)
            {
                x <<= 3u;
                x = (x & kHashModulus) + (x >> kHashBits);
                if (x >= kHashModulus)
                    x -= kHashModulus;
                auto l = (aValue.data[i] & kHashModulus) + (aValue.data[i] >> kHashBits);
                if (l >= kHashModulus)
                    l -= kHashModulus;
                x += l;
                if (x >= kHashModulus)
                    x -= kHashModulus;
            }
            return finished_hash(aValue.negative ? static_cast<std::uint64_t>(-static_cast<long long>(x)) : x);
        }

        long long big_hash(big_integer const& aValue)
        {
            big_integer q, r;
            big_divmod(big_abs(aValue), big_integer{ static_cast<long long>(kHashModulus) }, q, r);
            auto const magnitude = static_cast<std::uint64_t>(big_detail::cword(r, 0u));
            return finished_hash(big_negative(aValue) ? static_cast<std::uint64_t>(-static_cast<long long>(magnitude)) : magnitude);
        }

        long long float_hash(object* aObject, double aValue)
        {
            // (_Py_HashDouble)
            if (!std::isfinite(aValue))
                return std::isinf(aValue) ? (aValue > 0.0 ? kHashInf : -kHashInf) : pointer_hash(aObject);
            int e = 0;
            double m = std::frexp(aValue, &e);
            int sign = 1;
            if (m < 0.0)
            {
                sign = -1;
                m = -m;
            }
            std::uint64_t x = 0u;
            while (m != 0.0)
            {
                x = ((x << 28) & kHashModulus) | x >> (kHashBits - 28);
                m *= 268435456.0;
                e -= 28;
                auto const y = static_cast<std::uint64_t>(m);
                m -= static_cast<double>(y);
                x += y;
                if (x >= kHashModulus)
                    x -= kHashModulus;
            }
            e = e >= 0 ? e % kHashBits : kHashBits - 1 - ((-1 - e) % kHashBits);
            x = ((x << e) & kHashModulus) | x >> (kHashBits - e);
            x = x * static_cast<std::uint64_t>(static_cast<long long>(sign));
            return finished_hash(x);
        }

        // (CPython's SipHash-1-3, keyed as CPython's hash secret is when its hash randomization is disabled (PYTHONHASHSEED=0))
        std::uint64_t siphash13(std::uint64_t k0, std::uint64_t k1, unsigned char const* aData, std::size_t aSize)
        {
            auto const rotate = [](std::uint64_t x, int b) { return (x << b) | (x >> (64 - b)); };
            std::uint64_t b = static_cast<std::uint64_t>(aSize) << 56;
            std::uint64_t v0 = k0 ^ 0x736f6d6570736575ull;
            std::uint64_t v1 = k1 ^ 0x646f72616e646f6dull;
            std::uint64_t v2 = k0 ^ 0x6c7967656e657261ull;
            std::uint64_t v3 = k1 ^ 0x7465646279746573ull;
            auto const round = [&]()
            {
                v0 += v1; v2 += v3; v1 = rotate(v1, 13) ^ v0; v3 = rotate(v3, 16) ^ v2; v0 = rotate(v0, 32);
                v2 += v1; v0 += v3; v1 = rotate(v1, 17) ^ v2; v3 = rotate(v3, 21) ^ v0; v2 = rotate(v2, 32);
            };
            auto const word = [](unsigned char const* p, std::size_t n)
            {
                std::uint64_t w = 0u;
                for (std::size_t i = 0u; i < n; ++i)
                    w |= static_cast<std::uint64_t>(p[i]) << (8u * i);
                return w;
            };
            for (; aSize >= 8u; aData += 8, aSize -= 8u)
            {
                auto const mi = word(aData, 8u);
                v3 ^= mi;
                round();
                v0 ^= mi;
            }
            b |= word(aData, aSize);
            v3 ^= b;
            round();
            v0 ^= b;
            v2 ^= 0xff;
            round();
            round();
            round();
            return (v0 ^ v1) ^ (v2 ^ v3);
        }

        long long bytes_hash(std::string_view aBytes)
        {
            // (_Py_HashBytes)
            if (aBytes.empty())
                return 0;
            auto const x = static_cast<long long>(siphash13(0u, 0u, reinterpret_cast<unsigned char const*>(aBytes.data()), aBytes.size()));
            return x == -1 ? -2 : x;
        }

        long long str_hash(std::string_view aText)
        {
            // (CPython hashes a str's canonical representation: its code points as 1, 2 or 4 bytes each (little-endian), 
            // as its largest needs; ASCII's is its UTF-8)
            if (std::all_of(aText.begin(), aText.end(), [](char c) { return (static_cast<unsigned char>(c) & 0x80u) == 0u; }))
                return bytes_hash(aText);
            std::vector<char32_t> codePoints;
            char32_t largest = 0;
            for (std::size_t i = 0u; i < aText.size();)
            {
                auto const c = static_cast<unsigned char>(aText[i]);
                std::size_t const n = c < 0x80u ? 1u : c < 0xE0u ? 2u : c < 0xF0u ? 3u : 4u;
                char32_t cp = n == 1u ? c : n == 2u ? (c & 0x1Fu) : n == 3u ? (c & 0x0Fu) : (c & 0x07u);
                for (std::size_t j = 1u; j < n && i + j < aText.size(); ++j)
                    cp = (cp << 6) | (static_cast<unsigned char>(aText[i + j]) & 0x3Fu);
                codePoints.push_back(cp);
                largest = std::max(largest, cp);
                i += n;
            }
            std::size_t const width = largest < 0x100u ? 1u : largest < 0x10000u ? 2u : 4u;
            if (width == 1u && largest < 0x80u)
                return bytes_hash(aText);
            std::string data;
            data.reserve(codePoints.size() * width);
            for (auto cp : codePoints)
                for (std::size_t k = 0u; k < width; ++k)
                    data += static_cast<char>((cp >> (8u * k)) & 0xFFu);
            return bytes_hash(data);
        }

        bool hash_of(object* aObject, long long& aResult);

        bool tuple_hash(std::vector<object*> const& aItems, long long& aResult)
        {
            // (tuplehash: xxHash)
            constexpr std::uint64_t prime1 = 11400714785074694791ull;
            constexpr std::uint64_t prime2 = 14029467366897019727ull;
            constexpr std::uint64_t prime5 = 2870177450012600261ull;
            std::uint64_t acc = prime5;
            for (auto item : aItems)
            {
                long long lane = 0;
                if (!hash_of(item, lane))
                    return false;
                acc += static_cast<std::uint64_t>(lane) * prime2;
                acc = (acc << 31) | (acc >> 33);
                acc *= prime1;
            }
            acc += static_cast<std::uint64_t>(aItems.size()) ^ (prime5 ^ 3527539ull);
            aResult = acc == static_cast<std::uint64_t>(-1) ? 1546275796 : static_cast<long long>(acc);
            return true;
        }

        bool unhashable(object* aObject)
        {
            error("TypeError", std::string{ "unhashable type: '" } + type_name(aObject) + "'");
            return false;
        }

        bool hash_of(object* aObject, long long& aResult)
        {
            switch (kind_of(aObject))
            {
            case NEOS_PY_NONE:
                aResult = 0xFCA86420;
                return true;
            case NEOS_PY_BOOL:
                aResult = aObject == &sTrue.base ? 1 : 0;
                return true;
            case NEOS_PY_INT:
                if (is_big(aObject))
                {
                    limb_view v;
                    aResult = limbs_of(aObject, v) ? limbs_hash(v) : big_hash(big_of(aObject));
                }
                else
                    aResult = int_hash(as<int_object>(aObject)->value);
                return true;
            case NEOS_PY_FLOAT:
                aResult = float_hash(aObject, as<float_object>(aObject)->value);
                return true;
            case NEOS_PY_COMPLEX:
                {
                    auto const real = static_cast<std::uint64_t>(float_hash(aObject, as<complex_object>(aObject)->real));
                    auto const imag = static_cast<std::uint64_t>(float_hash(aObject, as<complex_object>(aObject)->imag));
                    aResult = finished_hash(real + kHashImag * imag);
                    return true;
                }
            case NEOS_PY_STR:
                {
                    // (found once: a str is immutable)
                    auto const& s = *as<str_object>(aObject);
                    if (!s.hashed)
                    {
                        s.hash = str_hash(s.value);
                        s.hashed = true;
                    }
                    aResult = s.hash;
                    return true;
                }
            case NEOS_PY_TUPLE:
                return tuple_hash(as<tuple_object>(aObject)->items, aResult);
            case NEOS_PY_RANGE:
                {
                    // (range_hash: (length, start, step), None for those a range's length makes irrelevant)
                    auto const& r = *as<range_object>(aObject);
                    auto const n = length(aObject);
                    ref const len{ make_int(n) };
                    ref const start{ n == 0 ? (retain(none()), none()) : make_int(r.start) };
                    ref const step{ n <= 1 ? (retain(none()), none()) : make_int(r.step) };
                    return tuple_hash({ len.get(), start.get(), step.get() }, aResult);
                }
            case NEOS_PY_LIST:
            case NEOS_PY_DICT:
            case NEOS_PY_SLICE:
                return unhashable(aObject);
            case NEOS_PY_OTHER:
                break;
            default:
                aResult = pointer_hash(aObject);
                return true;
            }
            if (is_instance(aObject))
            {
                // (a user class's instance: __hash__; a class that defines __eq__ but not __hash__ is unhashable)
                auto const cls = as<instance_object>(aObject)->cls;
                auto const hash = class_lookup(cls, "__hash__");
                if (hash != nullptr)
                {
                    if (kind_of(hash) == NEOS_PY_NONE)
                        return unhashable(aObject);
                    bool found = false;
                    ref const result{ call_special(aObject, "__hash__", {}, found) };
                    if (!result)
                        return false;
                    if (kind_of(result.get()) != NEOS_PY_INT && kind_of(result.get()) != NEOS_PY_BOOL)
                    {
                        error("TypeError", "__hash__ method should return an integer");
                        return false;
                    }
                    // (a 64-bit result is the hash (as CPython's Py_ssize_t); a larger one's hash is the hash)
                    if (is_small_int(result.get()))
                    {
                        aResult = as<int_object>(result.get())->value == -1 ? -2 : as<int_object>(result.get())->value;
                        return true;
                    }
                    return hash_of(result.get(), aResult);
                }
                if (class_lookup(cls, "__eq__") != nullptr)
                    return unhashable(aObject);
                if (auto const value = builtin_value(aObject); value != aObject)
                {
                    if (is_mutable_builtin(as<class_object>(cls)->builtinBase))
                        return unhashable(aObject);
                    return hash_of(value, aResult);
                }
                aResult = pointer_hash(aObject);
                return true;
            }
            if (auto const type = python_type(aObject); type != nullptr)
            {
                if (type->hash != nullptr)
                    return type->hash(context(), aObject, &aResult) == 0;
                if (type->compare != nullptr && !is_class(aObject) && !is_type_object(aObject) && aObject->type != &sExceptionType.base)
                    return unhashable(aObject);
            }
            aResult = pointer_hash(aObject);
            return true;
        }

        // a == b (PyObject_RichCompareBool: identity first): 1, 0 or -1 (an exception)
        int equal(object* aLhs, object* aRhs)
        {
            if (aLhs == aRhs)
                return 1;
            ref const result{ compare(NEOS_PY_EQ, aLhs, aRhs) };
            if (!result)
                return -1;
            return truth(result.get());
        }

        object* b_hash(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "hash") || !arity(aArgs, 1u, 1u, "hash"))
                return nullptr;
            long long h = 0;
            if (!hash_of(aArgs[0], h))
                return nullptr;
            return make_int(h);
        }

        object* b_id(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "id") || !arity(aArgs, 1u, 1u, "id"))
                return nullptr;
            return make_int(static_cast<long long>(reinterpret_cast<std::uintptr_t>(aArgs[0])));
        }

        // --- modules of Python's standard library (import math, sys): python::import

        struct module_object
        {
            header h;
            std::string name;
            std::vector<std::pair<std::string, object*>> attributes;
            // (a module of a source file: its names are those that its getter (a function of the program) reads, by their 
            // indexes (getter(-1): the names); its attributes are those set (e.g. its submodules'))
            object* getter = nullptr;
            std::vector<std::string> names;
            std::string file;
            bool initializing = false;
            bool main = false;      ///< (__main__: its attributes are its namespace's (globals()))
            ~module_object() { for (auto& a : attributes) release(a.second); release(getter); }
        };

        object* call_module_getter(object* aGetter, long long aIndex, object* aValue)
        {
            auto const value = aValue != nullptr ? aValue : none();
            retain(value);
            ref const args{ make_tuple({ make_int(aIndex), value, make_bool(aValue != nullptr) }) };
            return call(aGetter, args.get(), nullptr);
        }

        // (a module's name of a source file: its value (null: unbound, or an exception))
        object* module_value(module_object const& m, std::size_t aIndex, bool& aUnbound)
        {
            aUnbound = false;
            auto const value = call_module_getter(m.getter, static_cast<long long>(aIndex));
            if (value == nullptr && !failed())
                aUnbound = true;
            return value;
        }

        // (a module's name of a source file bound to a value (getter(index, value, True): e.g. module.name = value): 1, 0 if it 
        // cannot be (a function that a def defines), or -1 (an exception))
        int module_bind(module_object const& m, std::size_t aIndex, object* aValue)
        {
            ref const result{ call_module_getter(m.getter, static_cast<long long>(aIndex), aValue) };
            if (!result)
                return failed() ? -1 : 0;
            return result.get() == &sTrue.base ? 1 : 0;
        }

        neos_py_type make_module_type()
        {
            auto type = make_type("python.module", "module", retain_object, release_object<module_object>);
            type.repr = [](neos_py_context*, object* aSelf) -> object*
            {
                auto const& m = *as<module_object>(aSelf);
                if (m.getter != nullptr)
                    return make_str("<module '" + m.name + "' from '" + m.file + "'>");
                if (m.main)
                    return make_str("<module '__main__'>");
                return make_str("<module '" + m.name + "' (built-in)>");
            };
            type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
            {
                auto const& m = *as<module_object>(aSelf);
                if (m.main)
                {
                    ref ns{ fn_module_namespace("__main__", nullptr) };
                    if (!ns)
                        return nullptr;
                    if (std::strcmp(aName, "__dict__") == 0)
                        return ns.release_ownership();
                    ref const key{ make_str(aName) };
                    if (auto const position = dict_find(ns.get(), key.get()); position >= 0)
                    {
                        auto const value = as<dict_object>(ns.get())->items[static_cast<std::size_t>(position)].second;
                        retain(value);
                        return value;
                    }
                    else if (position == -2)
                        return nullptr;
                    // (__file__: the program's source (its outermost frame's))
                    if (std::strcmp(aName, "__file__") == 0)
                    {
                        auto const frames = capture_frames();
                        for (auto f = frames->rbegin(); f != frames->rend(); ++f)
                            if (f->function != nullptr && std::string_view{ f->function } == "<module>" && f->source != nullptr && *f->source != '\0' && *f->source != '<')
                                return make_str(f->source);
                    }
                    return error("AttributeError", "module '__main__' has no attribute '" + std::string{ aName } + "'");
                }
                if (std::strcmp(aName, "__name__") == 0)
                    return make_str(m.name);
                for (auto const& a : m.attributes)
                    if (a.first == aName)
                        return (retain(a.second), a.second);
                if (m.getter != nullptr)
                {
                    for (std::size_t i = 0u; i < m.names.size(); ++i)
                        if (m.names[i] == aName)
                        {
                            bool unbound = false;
                            auto const value = module_value(m, i, unbound);
                            if (value != nullptr || !unbound)
                                return value;
                            break;
                        }
                    if (std::strcmp(aName, "__file__") == 0)
                        return make_str(m.file);
                    if (std::strcmp(aName, "__package__") == 0)
                        return make_str(m.name.find('.') == std::string::npos ? std::string{} : m.name.substr(0u, m.name.rfind('.')));
                    if (std::strcmp(aName, "__doc__") == 0 || std::strcmp(aName, "__spec__") == 0 || std::strcmp(aName, "__loader__") == 0)
                        return (retain(none()), none());
                }
                if (std::strcmp(aName, "__dict__") == 0)
                {
                    auto const d = make_dict();
                    ref const nameKey{ make_str("__name__") };
                    ref const nameValue{ make_str(m.name) };
                    dict_set(d, nameKey.get(), nameValue.get());
                    for (std::size_t i = 0u; i < m.names.size(); ++i)
                    {
                        bool unbound = false;
                        ref const value{ module_value(m, i, unbound) };
                        if (!value)
                        {
                            if (!unbound)
                            {
                                release(d);
                                return nullptr;
                            }
                            continue;
                        }
                        ref const key{ make_str(m.names[i]) };
                        dict_set(d, key.get(), value.get());
                    }
                    for (auto const& a : m.attributes)
                    {
                        ref const key{ make_str(a.first) };
                        dict_set(d, key.get(), a.second);
                    }
                    return d;
                }
                if (m.initializing)
                    return error("AttributeError", "partially initialized module '" + m.name + "' has no attribute '" + aName + 
                        "' (most likely due to a circular import)");
                return error("AttributeError", "module '" + m.name + "' has no attribute '" + aName + "'");
            };
            type.setattr = [](neos_py_context*, object* aSelf, char const* aName, object* aValue) -> int
            {
                auto& m = *as<module_object>(aSelf);
                if (m.main)
                {
                    ref const ns{ fn_module_namespace("__main__", nullptr) };
                    if (!ns)
                        return -1;
                    ref const key{ make_str(aName) };
                    if (aValue != nullptr)
                        return dict_set(ns.get(), key.get(), aValue) == 0 ? 0 : -1;
                    auto const position = dict_find(ns.get(), key.get());
                    if (position == -2)
                        return -1;
                    if (position < 0)
                    {
                        error("AttributeError", "module '__main__' has no attribute '" + std::string{ aName } + "'");
                        return -1;
                    }
                    dict_erase(*as<dict_object>(ns.get()), static_cast<std::size_t>(position));
                    return 0;
                }
                if (m.getter != nullptr && aValue != nullptr)
                    for (std::size_t i = 0u; i < m.names.size(); ++i)
                        if (m.names[i] == aName)
                        {
                            auto const bound = module_bind(m, i, aValue);
                            if (bound != 0)
                                return bound > 0 ? 0 : -1;
                            break;
                        }
                for (auto a = m.attributes.begin(); a != m.attributes.end(); ++a)
                    if (a->first == aName)
                    {
                        auto const old = a->second;
                        if (aValue == nullptr)
                            m.attributes.erase(a);
                        else
                        {
                            retain(aValue);
                            a->second = aValue;
                        }
                        release(old);
                        return 0;
                    }
                if (aValue == nullptr)
                {
                    error("AttributeError", "module '" + m.name + "' has no attribute '" + aName + "'");
                    return -1;
                }
                retain(aValue);
                m.attributes.emplace_back(aName, aValue);
                return 0;
            };
            return type;
        }

        neos_py_type sModuleType = make_module_type();

        // (math: the algorithms of CPython's mathmodule.c (Python Software Foundation License))
        namespace math_module
        {
            double constexpr pi = 3.141592653589793238462643383279502884197;
            double constexpr logpi = 1.144729885849400174143427351353058711647;

            enum class math_error { None, Domain, Range };

            object* raise_math(math_error aError, double aResult)
            {
                if (aError == math_error::Domain)
                    return error("ValueError", "math domain error");
                if (aError == math_error::Range && std::fabs(aResult) >= 1.5)
                    return error("OverflowError", "math range error");
                return make_float(aResult);
            }

            bool arg_double(std::vector<object*> const& aArgs, std::size_t aIndex, double& aResult)
            {
                return to_double(aArgs[aIndex], aResult);
            }

            // (math_1: a NaN result from a non-NaN argument is a domain error; an infinite result from a finite argument is
            // a range error if the function can overflow, else a domain error)
            object* math_1(std::vector<object*> const& aArgs, char const* aName, double (*aFunction)(double), bool aCanOverflow)
            {
                if (!arity(aArgs, 1u, 1u, aName))
                    return nullptr;
                double x = 0.0;
                if (!arg_double(aArgs, 0, x))
                    return nullptr;
                auto const r = aFunction(x);
                if (std::isnan(r) && !std::isnan(x))
                    return error("ValueError", "math domain error");
                if (std::isinf(r) && std::isfinite(x))
                    return error(aCanOverflow ? "OverflowError" : "ValueError", aCanOverflow ? "math range error" : "math domain error");
                return make_float(r);
            }

            // (math_2: two arguments)
            object* math_2(std::vector<object*> const& aArgs, char const* aName, double (*aFunction)(double, double))
            {
                if (!arity(aArgs, 2u, 2u, aName))
                    return nullptr;
                double x = 0.0, y = 0.0;
                if (!arg_double(aArgs, 0, x) || !arg_double(aArgs, 1, y))
                    return nullptr;
                auto const r = aFunction(x, y);
                if (std::isnan(r) && !std::isnan(x) && !std::isnan(y))
                    return error("ValueError", "math domain error");
                if (std::isinf(r) && std::isfinite(x) && std::isfinite(y))
                    return raise_math(math_error::Range, r);
                return make_float(r);
            }

            double m_sinpi(double x)
            {
                double const y = std::fmod(std::fabs(x), 2.0);
                int const n = static_cast<int>(std::round(2.0 * y));
                double r = 0.0;
                switch (n)
                {
                case 0: r = std::sin(pi * y); break;
                case 1: r = std::cos(pi * (y - 0.5)); break;
                case 2: r = std::sin(pi * (1.0 - y)); break;
                case 3: r = -std::cos(pi * (y - 1.5)); break;
                default: r = std::sin(pi * (y - 2.0)); break;
                }
                return std::copysign(1.0, x) * r;
            }

            int constexpr LanczosN = 13;
            double constexpr lanczos_g = 6.024680040776729583740234375;
            double constexpr lanczos_g_minus_half = 5.524680040776729583740234375;
            double constexpr lanczos_num_coeffs[LanczosN] =
            {
                23531376880.410759688572007674451636754734846804940, 42919803642.649098768957899047001988850926355848959,
                35711959237.355668049440185451547166705960488635843, 17921034426.037209699919755754458931112671403265390,
                6039542586.3520280050642916443072979210699388420708, 1439720407.3117216736632230727949123939715485786772,
                248874557.86205415651146038641322942321632125127801, 31426415.585400194380614231628318205362874684987640,
                2876370.6289353724412254090516208496135991145378768, 186056.26539522349504029498971604569928220784236328,
                8071.6720023658162106380029022722506138218516325024, 210.82427775157934587250973392071336271166969580291,
                2.5066282746310002701649081771338373386264310793408
            };
            double constexpr lanczos_den_coeffs[LanczosN] =
            {
                0.0, 39916800.0, 120543840.0, 150917976.0, 105258076.0, 45995730.0, 13339535.0, 2637558.0, 357423.0, 32670.0, 1925.0, 66.0, 1.0
            };
            int constexpr NGammaIntegral = 23;
            double constexpr gamma_integral[NGammaIntegral] =
            {
                1.0, 1.0, 2.0, 6.0, 24.0, 120.0, 720.0, 5040.0, 40320.0, 362880.0, 3628800.0, 39916800.0, 479001600.0, 6227020800.0,
                87178291200.0, 1307674368000.0, 20922789888000.0, 355687428096000.0, 6402373705728000.0, 121645100408832000.0,
                2432902008176640000.0, 51090942171709440000.0, 1124000727777607680000.0
            };

            double lanczos_sum(double x)
            {
                double num = 0.0, den = 0.0;
                if (x < 5.0)
                    for (int i = LanczosN; --i >= 0;)
                    {
                        num = num * x + lanczos_num_coeffs[i];
                        den = den * x + lanczos_den_coeffs[i];
                    }
                else
                    for (int i = 0; i < LanczosN; i++)
                    {
                        num = num / x + lanczos_num_coeffs[i];
                        den = den / x + lanczos_den_coeffs[i];
                    }
                return num / den;
            }

            double m_tgamma(double x, math_error& aError)
            {
                if (!std::isfinite(x))
                {
                    if (std::isnan(x) || x > 0.0)
                        return x;
                    aError = math_error::Domain;
                    return std::numeric_limits<double>::quiet_NaN();
                }
                if (x == 0.0)
                {
                    aError = math_error::Domain;
                    return std::copysign(std::numeric_limits<double>::infinity(), x);
                }
                if (x == std::floor(x))
                {
                    if (x < 0.0)
                    {
                        aError = math_error::Domain;
                        return std::numeric_limits<double>::quiet_NaN();
                    }
                    if (x <= NGammaIntegral)
                        return gamma_integral[static_cast<int>(x) - 1];
                }
                double const absx = std::fabs(x);
                if (absx < 1e-20)
                {
                    double const r = 1.0 / x;
                    if (std::isinf(r))
                        aError = math_error::Range;
                    return r;
                }
                if (absx > 200.0)
                {
                    if (x < 0.0)
                        return 0.0 / m_sinpi(x);
                    aError = math_error::Range;
                    return std::numeric_limits<double>::infinity();
                }
                double const y = absx + lanczos_g_minus_half;
                double z;
                if (absx > lanczos_g_minus_half)
                {
                    double const q = y - absx;
                    z = q - lanczos_g_minus_half;
                }
                else
                {
                    double const q = y - lanczos_g_minus_half;
                    z = q - absx;
                }
                z = z * lanczos_g / y;
                double r;
                if (x < 0.0)
                {
                    r = -pi / m_sinpi(absx) / absx * std::exp(y) / lanczos_sum(absx);
                    r -= z * r;
                    if (absx < 140.0)
                        r /= std::pow(y, absx - 0.5);
                    else
                    {
                        double const sqrtpow = std::pow(y, absx / 2.0 - 0.25);
                        r /= sqrtpow;
                        r /= sqrtpow;
                    }
                }
                else
                {
                    r = lanczos_sum(absx) / std::exp(y);
                    r += z * r;
                    if (absx < 140.0)
                        r *= std::pow(y, absx - 0.5);
                    else
                    {
                        double const sqrtpow = std::pow(y, absx / 2.0 - 0.25);
                        r *= sqrtpow;
                        r *= sqrtpow;
                    }
                }
                if (std::isinf(r))
                    aError = math_error::Range;
                return r;
            }

            double m_lgamma(double x, math_error& aError)
            {
                if (!std::isfinite(x))
                    return std::isnan(x) ? x : std::numeric_limits<double>::infinity();
                if (x == std::floor(x) && x <= 2.0)
                {
                    if (x <= 0.0)
                    {
                        aError = math_error::Domain;
                        return std::numeric_limits<double>::infinity();
                    }
                    return 0.0;
                }
                double const absx = std::fabs(x);
                if (absx < 1e-20)
                    return -std::log(absx);
                double r = std::log(lanczos_sum(absx)) - lanczos_g;
                r += (absx - 0.5) * (std::log(absx + lanczos_g - 0.5) - 1);
                if (x < 0.0)
                    r = logpi - std::log(std::fabs(m_sinpi(absx))) - std::log(absx) - r;
                if (std::isinf(r))
                    aError = math_error::Range;
                return r;
            }

            double m_atan2(double y, double x)
            {
                if (std::isnan(x) || std::isnan(y))
                    return std::numeric_limits<double>::quiet_NaN();
                if (std::isinf(y))
                {
                    if (std::isinf(x))
                        return std::copysign(std::copysign(1.0, x) == 1.0 ? 0.25 * pi : 0.75 * pi, y);
                    return std::copysign(0.5 * pi, y);
                }
                if (std::isinf(x) || y == 0.0)
                    return std::copysign(1.0, x) == 1.0 ? std::copysign(0.0, y) : std::copysign(pi, y);
                return std::atan2(y, x);
            }

            double m_remainder(double x, double y)
            {
                if (std::isfinite(x) && std::isfinite(y))
                {
                    if (y == 0.0)
                        return std::numeric_limits<double>::quiet_NaN();
                    double const absx = std::fabs(x), absy = std::fabs(y);
                    double const m = std::fmod(absx, absy);
                    double const c = absy - m;
                    double r;
                    if (m < c)
                        r = m;
                    else if (m > c)
                        r = -c;
                    else
                        r = m - 2.0 * std::fmod(0.5 * (absx - m), absy);
                    return std::copysign(1.0, x) * r;
                }
                if (std::isnan(x))
                    return x;
                if (std::isnan(y))
                    return y;
                if (std::isinf(x))
                    return std::numeric_limits<double>::quiet_NaN();
                return x;
            }

            double m_log(double x)
            {
                if (std::isfinite(x))
                {
                    if (x > 0.0)
                        return std::log(x);
                    return x == 0.0 ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::quiet_NaN();
                }
                if (std::isnan(x) || x > 0.0)
                    return x;
                return std::numeric_limits<double>::quiet_NaN();
            }

            double m_log2(double x)
            {
                if (!std::isfinite(x))
                    return std::isnan(x) || x > 0.0 ? x : std::numeric_limits<double>::quiet_NaN();
                if (x > 0.0)
                    return std::log2(x);
                return x == 0.0 ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::quiet_NaN();
            }

            double m_log10(double x)
            {
                if (std::isfinite(x))
                {
                    if (x > 0.0)
                        return std::log10(x);
                    return x == 0.0 ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::quiet_NaN();
                }
                if (std::isnan(x) || x > 0.0)
                    return x;
                return std::numeric_limits<double>::quiet_NaN();
            }

            double m_log1p(double x)
            {
                // (as C99: log1p(-1) is -inf (divide-by-zero))
                return std::log1p(x);
            }

            struct double_length { double hi; double lo; };

            double_length dl_fast_sum(double a, double b)
            {
                double const x = a + b;
                double const y = (a - x) + b;
                return { x, y };
            }

            double_length dl_sum(double a, double b)
            {
                double const x = a + b;
                double const z = x - a;
                double const y = (a - (x - z)) + (b - z);
                return { x, y };
            }

            double_length dl_mul(double x, double y)
            {
                double const z = x * y;
                double const zz = std::fma(x, y, -z);
                return { z, zz };
            }

            struct triple_length { double hi; double lo; double tiny; };

            triple_length tl_fma(double x, double y, triple_length total)
            {
                auto const pr = dl_mul(x, y);
                auto const sm = dl_sum(total.hi, pr.hi);
                auto const r1 = dl_sum(total.lo, pr.lo);
                auto const r2 = dl_sum(r1.hi, sm.lo);
                return { sm.hi, r2.hi, total.tiny + r1.lo + r2.lo };
            }

            double tl_to_d(triple_length total)
            {
                auto const last = dl_sum(total.lo, total.hi);
                return total.tiny + last.lo + last.hi;
            }

            double vector_norm(std::vector<double>& vec, double max, bool foundNan)
            {
                auto const n = vec.size();
                if (std::isinf(max))
                    return max;
                if (foundNan)
                    return std::numeric_limits<double>::quiet_NaN();
                if (max == 0.0 || n <= 1u)
                    return max;
                int maxE = 0;
                std::frexp(max, &maxE);
                if (maxE < -1023)
                {
                    for (auto& v : vec)
                        v /= std::numeric_limits<double>::min();
                    return std::numeric_limits<double>::min() * vector_norm(vec, max / std::numeric_limits<double>::min(), foundNan);
                }
                double const scale = std::ldexp(1.0, -maxE);
                double csum = 1.0, frac1 = 0.0, frac2 = 0.0;
                for (auto x : vec)
                {
                    x *= scale;
                    auto const pr = dl_mul(x, x);
                    auto const sm = dl_fast_sum(csum, pr.hi);
                    csum = sm.hi;
                    frac1 += pr.lo;
                    frac2 += sm.lo;
                }
                double h = std::sqrt(csum - 1.0 + (frac1 + frac2));
                auto const pr = dl_mul(-h, h);
                auto const sm = dl_fast_sum(csum, pr.hi);
                csum = sm.hi;
                frac1 += pr.lo;
                frac2 += sm.lo;
                double const x = csum - 1.0 + (frac1 + frac2);
                h += x / (2.0 * h);
                return h / scale;
            }

            // (int(floor(x)) etc.: a float as an int)
            object* int_from_double(double x)
            {
                if (std::isnan(x))
                    return error("ValueError", "cannot convert float NaN to integer");
                if (std::isinf(x))
                    return error("OverflowError", "cannot convert float infinity to integer");
                if (x >= 9223372036854775808.0 || x < -9223372036854775808.0)
                    return make_int(big_from_double(x));
                return make_int(static_cast<long long>(x));
            }

            object* rounding(std::vector<object*> const& aArgs, char const* aName, char const* aSpecial, double (*aFunction)(double))
            {
                if (!arity(aArgs, 1u, 1u, aName))
                    return nullptr;
                auto const x = aArgs[0];
                if (is_big(x))
                    return (retain(x), x);
                if (is_int_like(x))
                    return make_int(int_value(x));
                if (kind_of(x) != NEOS_PY_FLOAT)
                {
                    if (ref const method{ special_method(x, aSpecial) })
                    {
                        ref const args{ make_tuple() };
                        return call(method.get(), args.get(), nullptr);
                    }
                    // (a type of a module (e.g. fractions.Fraction): its method)
                    if (!is_instance(x) && kind_of(x) == NEOS_PY_OTHER && python_type(x) != nullptr && python_type(x)->getattr != nullptr)
                    {
                        if (ref const method{ python_type(x)->getattr(context(), x, aSpecial) })
                        {
                            ref const args{ make_tuple() };
                            return call(method.get(), args.get(), nullptr);
                        }
                        clear_error();
                    }
                }
                double d = 0.0;
                if (kind_of(x) == NEOS_PY_OTHER && std::string_view{ aSpecial } == "__trunc__" && python_type(x) != nullptr && python_type(x)->to_float == nullptr)
                    return error("TypeError", std::string{ "type " } + type_name(x) + " doesn't define __trunc__ method");
                if (!to_double(x, d))
                {
                    if (std::string_view{ aSpecial } == "__trunc__")
                    {
                        clear_error();
                        return error("TypeError", std::string{ "type " } + type_name(x) + " doesn't define __trunc__ method");
                    }
                    return nullptr;
                }
                return int_from_double(aFunction(d));
            }

            bool arg_index(std::vector<object*> const& aArgs, std::size_t aIndex, long long& aResult)
            {
                return to_index(aArgs[aIndex], aResult);
            }

            object* m_floor(std::vector<object*> const& a, object*) { return rounding(a, "floor", "__floor__", [](double x) { return std::floor(x); }); }
            object* m_ceil(std::vector<object*> const& a, object*) { return rounding(a, "ceil", "__ceil__", [](double x) { return std::ceil(x); }); }
            object* m_trunc(std::vector<object*> const& a, object*) { return rounding(a, "trunc", "__trunc__", [](double x) { return std::trunc(x); }); }
            object* m_sqrt(std::vector<object*> const& a, object*) { return math_1(a, "sqrt", [](double x) { return std::sqrt(x); }, false); }
            object* m_cbrt(std::vector<object*> const& a, object*) { return math_1(a, "cbrt", [](double x) { return std::cbrt(x); }, false); }
            object* m_exp(std::vector<object*> const& a, object*) { return math_1(a, "exp", [](double x) { return std::exp(x); }, true); }
            object* m_exp2(std::vector<object*> const& a, object*) { return math_1(a, "exp2", [](double x) { return std::exp2(x); }, true); }
            object* m_expm1(std::vector<object*> const& a, object*) { return math_1(a, "expm1", [](double x) { return std::expm1(x); }, true); }
            object* m_log1p_(std::vector<object*> const& a, object*) { return math_1(a, "log1p", m_log1p, false); }
            object* m_fabs(std::vector<object*> const& a, object*) { return math_1(a, "fabs", [](double x) { return std::fabs(x); }, false); }
            object* m_sin(std::vector<object*> const& a, object*) { return math_1(a, "sin", [](double x) { return std::sin(x); }, false); }
            object* m_cos(std::vector<object*> const& a, object*) { return math_1(a, "cos", [](double x) { return std::cos(x); }, false); }
            object* m_tan(std::vector<object*> const& a, object*) { return math_1(a, "tan", [](double x) { return std::tan(x); }, false); }
            object* m_asin(std::vector<object*> const& a, object*) { return math_1(a, "asin", [](double x) { return std::asin(x); }, false); }
            object* m_acos(std::vector<object*> const& a, object*) { return math_1(a, "acos", [](double x) { return std::acos(x); }, false); }
            object* m_atan(std::vector<object*> const& a, object*) { return math_1(a, "atan", [](double x) { return std::atan(x); }, false); }
            object* m_sinh(std::vector<object*> const& a, object*) { return math_1(a, "sinh", [](double x) { return std::sinh(x); }, true); }
            object* m_cosh(std::vector<object*> const& a, object*) { return math_1(a, "cosh", [](double x) { return std::cosh(x); }, true); }
            object* m_tanh(std::vector<object*> const& a, object*) { return math_1(a, "tanh", [](double x) { return std::tanh(x); }, false); }
            object* m_asinh(std::vector<object*> const& a, object*) { return math_1(a, "asinh", [](double x) { return std::asinh(x); }, false); }
            object* m_acosh(std::vector<object*> const& a, object*) { return math_1(a, "acosh", [](double x) { return std::acosh(x); }, false); }
            object* m_atanh(std::vector<object*> const& a, object*) { return math_1(a, "atanh", [](double x) { return std::atanh(x); }, false); }
            object* m_erf(std::vector<object*> const& a, object*) { return math_1(a, "erf", [](double x) { return std::erf(x); }, false); }
            object* m_erfc(std::vector<object*> const& a, object*) { return math_1(a, "erfc", [](double x) { return std::erfc(x); }, false); }
            object* m_log2_(std::vector<object*> const& a, object*) { return math_1(a, "log2", m_log2, false); }
            object* m_log10_(std::vector<object*> const& a, object*) { return math_1(a, "log10", m_log10, false); }
            object* m_degrees(std::vector<object*> const& a, object*) { return math_1(a, "degrees", [](double x) { return x * (180.0 / pi); }, true); }
            object* m_radians(std::vector<object*> const& a, object*) { return math_1(a, "radians", [](double x) { return x * (pi / 180.0); }, false); }
            object* m_atan2_(std::vector<object*> const& a, object*) { return math_2(a, "atan2", m_atan2); }
            object* m_copysign(std::vector<object*> const& a, object*) { return math_2(a, "copysign", [](double x, double y) { return std::copysign(x, y); }); }
            object* m_remainder_(std::vector<object*> const& a, object*) { return math_2(a, "remainder", m_remainder); }

            object* gamma_function(std::vector<object*> const& aArgs, char const* aName, double (*aFunction)(double, math_error&))
            {
                if (!arity(aArgs, 1u, 1u, aName))
                    return nullptr;
                double x = 0.0;
                if (!arg_double(aArgs, 0, x))
                    return nullptr;
                auto e = math_error::None;
                auto const r = aFunction(x, e);
                return raise_math(e, r);
            }

            object* m_gamma(std::vector<object*> const& a, object*) { return gamma_function(a, "gamma", m_tgamma); }
            object* m_lgamma_(std::vector<object*> const& a, object*) { return gamma_function(a, "lgamma", m_lgamma); }

            object* m_fmod(std::vector<object*> const& aArgs, object*)
            {
                if (!arity(aArgs, 2u, 2u, "fmod"))
                    return nullptr;
                double x = 0.0, y = 0.0;
                if (!arg_double(aArgs, 0, x) || !arg_double(aArgs, 1, y))
                    return nullptr;
                if (std::isinf(y) && std::isfinite(x))
                    return make_float(x);
                auto r = std::fmod(x, y);
                if (r == 0.0 && y != 0.0)
                    r = std::copysign(r, x);
                if (std::isnan(r) && !std::isnan(x) && !std::isnan(y))
                    return error("ValueError", "math domain error");
                return make_float(r);
            }

            object* m_pow(std::vector<object*> const& aArgs, object*)
            {
                if (!arity(aArgs, 2u, 2u, "pow"))
                    return nullptr;
                double x = 0.0, y = 0.0;
                if (!arg_double(aArgs, 0, x) || !arg_double(aArgs, 1, y))
                    return nullptr;
                double r = 0.0;
                auto e = math_error::None;
                if (!std::isfinite(x) || !std::isfinite(y))
                {
                    if (std::isnan(x))
                        r = y == 0.0 ? 1.0 : x;
                    else if (std::isnan(y))
                        r = x == 1.0 ? 1.0 : y;
                    else if (std::isinf(x))
                    {
                        bool const oddY = std::isfinite(y) && std::fmod(std::fabs(y), 2.0) == 1.0;
                        if (y > 0.0)
                            r = oddY ? x : std::fabs(x);
                        else if (y == 0.0)
                            r = 1.0;
                        else
                            r = oddY ? std::copysign(0.0, x) : 0.0;
                    }
                    else if (std::fabs(x) == 1.0)
                        r = 1.0;
                    else if (y > 0.0 && std::fabs(x) > 1.0)
                        r = y;
                    else if (y < 0.0 && std::fabs(x) < 1.0)
                        r = -y;
                    else
                        r = 0.0;
                }
                else
                {
                    r = std::pow(x, y);
                    if (std::isnan(r))
                        e = math_error::Domain;
                    else if (std::isinf(r))
                        e = x == 0.0 ? math_error::Domain : math_error::Range;
                }
                return raise_math(e, r);
            }

            // (log of an int or a float: a domain error if it is not positive)
            bool loghelper(object* aArg, double (*aFunction)(double), double& aResult)
            {
                if (is_int_like(aArg) && (is_big(aArg) ? big_negative(big_of(aArg)) : int_value(aArg) <= 0))
                {
                    error("ValueError", "math domain error");
                    return false;
                }
                double x = 0.0;
                if (is_big(aArg) && !big_to_double(big_of(aArg), x))
                {
                    // (as CPython: too large for a double: x = m * 2^e (0.5 <= m < 1), log(x) = log(m) + log(2) * e)
                    auto const e = static_cast<std::int64_t>(big_bit_length(big_of(aArg)));
                    double m = 0.0;
                    big_to_double(big_abs(big_of(aArg)), -e, false, false, m);
                    aResult = aFunction(m) + aFunction(2.0) * static_cast<double>(e);
                    return true;
                }
                if (!to_double(aArg, x))
                    return false;
                auto const r = aFunction(x);
                if (std::isnan(r) && !std::isnan(x))
                {
                    error("ValueError", "math domain error");
                    return false;
                }
                if (std::isinf(r) && std::isfinite(x))
                {
                    error("ValueError", "math domain error");
                    return false;
                }
                aResult = r;
                return true;
            }

            object* m_log_(std::vector<object*> const& aArgs, object*)
            {
                if (!arity(aArgs, 1u, 2u, "log"))
                    return nullptr;
                double num = 0.0;
                if (!loghelper(aArgs[0], m_log, num))
                    return nullptr;
                if (aArgs.size() == 1u)
                    return make_float(num);
                double den = 0.0;
                if (!loghelper(aArgs[1], m_log, den))
                    return nullptr;
                if (den == 0.0)
                    return error("ZeroDivisionError", "float division by zero");
                return make_float(num / den);
            }

            object* m_frexp(std::vector<object*> const& aArgs, object*)
            {
                if (!arity(aArgs, 1u, 1u, "frexp"))
                    return nullptr;
                double x = 0.0;
                if (!arg_double(aArgs, 0, x))
                    return nullptr;
                int e = 0;
                double m = x;
                if (!(std::isnan(x) || std::isinf(x) || x == 0.0))
                    m = std::frexp(x, &e);
                return make_tuple({ make_float(m), make_int(e) });
            }

            object* m_ldexp(std::vector<object*> const& aArgs, object*)
            {
                if (!arity(aArgs, 2u, 2u, "ldexp"))
                    return nullptr;
                double x = 0.0;
                if (!arg_double(aArgs, 0, x))
                    return nullptr;
                if (!is_int_like(aArgs[1]))
                    return error("TypeError", "Expected an int as second argument to ldexp.");
                auto const e = is_big(aArgs[1]) ? (big_negative(big_of(aArgs[1])) ? std::numeric_limits<long long>::min() : std::numeric_limits<long long>::max()) : int_value(aArgs[1]);
                double r;
                if (x == 0.0 || !std::isfinite(x))
                    r = x;
                else if (e > std::numeric_limits<int>::max())
                    return error("OverflowError", "math range error");
                else if (e < std::numeric_limits<int>::min())
                    r = std::copysign(0.0, x);
                else
                {
                    r = std::ldexp(x, static_cast<int>(e));
                    if (std::isinf(r))
                        return error("OverflowError", "math range error");
                }
                return make_float(r);
            }

            object* m_modf(std::vector<object*> const& aArgs, object*)
            {
                if (!arity(aArgs, 1u, 1u, "modf"))
                    return nullptr;
                double x = 0.0;
                if (!arg_double(aArgs, 0, x))
                    return nullptr;
                if (std::isinf(x))
                    return make_tuple({ make_float(std::copysign(0.0, x)), make_float(x) });
                double i = 0.0;
                double const f = std::modf(x, &i);
                return make_tuple({ make_float(f), make_float(i) });
            }

            object* m_hypot(std::vector<object*> const& aArgs, object*)
            {
                std::vector<double> coordinates;
                double max = 0.0;
                bool foundNan = false;
                for (auto a : aArgs)
                {
                    double x = 0.0;
                    if (!to_double(a, x))
                        return nullptr;
                    x = std::fabs(x);
                    coordinates.push_back(x);
                    foundNan = foundNan || std::isnan(x);
                    if (x > max)
                        max = x;
                }
                return make_float(vector_norm(coordinates, max, foundNan));
            }

            object* m_dist(std::vector<object*> const& aArgs, object*)
            {
                if (!arity(aArgs, 2u, 2u, "dist"))
                    return nullptr;
                std::vector<ref> p, q;
                if (!items_of(aArgs[0], p) || !items_of(aArgs[1], q))
                    return nullptr;
                if (p.size() != q.size())
                    return error("ValueError", "both points must have the same number of dimensions");
                std::vector<double> diffs;
                double max = 0.0;
                bool foundNan = false;
                for (std::size_t i = 0u; i < p.size(); ++i)
                {
                    double px = 0.0, qx = 0.0;
                    if (!to_double(p[i].get(), px) || !to_double(q[i].get(), qx))
                        return nullptr;
                    auto const x = std::fabs(px - qx);
                    diffs.push_back(x);
                    foundNan = foundNan || std::isnan(x);
                    if (x > max)
                        max = x;
                }
                return make_float(vector_norm(diffs, max, foundNan));
            }

            object* m_fsum(std::vector<object*> const& aArgs, object*)
            {
                if (!arity(aArgs, 1u, 1u, "fsum"))
                    return nullptr;
                std::vector<ref> items;
                if (!items_of(aArgs[0], items))
                    return nullptr;
                std::vector<double> p;
                double specialSum = 0.0, infSum = 0.0, lo = 0.0;
                for (auto const& item : items)
                {
                    double x = 0.0;
                    if (!to_double(item.get(), x))
                        return nullptr;
                    double const xsave = x;
                    std::size_t i = 0u;
                    for (std::size_t j = 0u; j < p.size(); ++j)
                    {
                        double y = p[j];
                        if (std::fabs(x) < std::fabs(y))
                            std::swap(x, y);
                        double const hi = x + y;
                        double const yr = hi - x;
                        lo = y - yr;
                        if (lo != 0.0)
                            p[i++] = lo;
                        x = hi;
                    }
                    p.resize(i);
                    if (x != 0.0)
                    {
                        if (!std::isfinite(x))
                        {
                            if (std::isfinite(xsave))
                                return error("OverflowError", "intermediate overflow in fsum");
                            if (std::isinf(xsave))
                                infSum += xsave;
                            specialSum += xsave;
                            p.clear();
                        }
                        else
                            p.push_back(x);
                    }
                }
                if (specialSum != 0.0)
                {
                    if (std::isnan(infSum))
                        return error("ValueError", "-inf + inf in fsum");
                    return make_float(specialSum);
                }
                double hi = 0.0;
                if (!p.empty())
                {
                    auto n = p.size();
                    hi = p[--n];
                    while (n > 0u)
                    {
                        double const x = hi;
                        double const y = p[--n];
                        hi = x + y;
                        double const yr = hi - x;
                        lo = y - yr;
                        if (lo != 0.0)
                            break;
                    }
                    if (n > 0u && ((lo < 0.0 && p[n - 1u] < 0.0) || (lo > 0.0 && p[n - 1u] > 0.0)))
                    {
                        double const y = lo * 2.0;
                        double const x = hi + y;
                        double const yr = x - hi;
                        if (y == yr)
                            hi = x;
                    }
                }
                return make_float(hi);
            }

            object* m_prod(std::vector<object*> const& aArgs, object* aKwargs)
            {
                object* start = nullptr;
                if (!keywords(aKwargs, { { "start", &start } }, "prod") || !arity(aArgs, 1u, 1u, "prod"))
                    return nullptr;
                std::vector<ref> items;
                if (!items_of(aArgs[0], items))
                    return nullptr;
                ref result{ start != nullptr ? (retain(start), start) : make_int(1) };
                for (auto const& item : items)
                {
                    result = ref{ binary(NEOS_PY_MULTIPLY, result.get(), item.get(), false) };
                    if (!result)
                        return nullptr;
                }
                return result.release_ownership();
            }

            object* m_sumprod(std::vector<object*> const& aArgs, object*)
            {
                if (!arity(aArgs, 2u, 2u, "sumprod"))
                    return nullptr;
                std::vector<ref> p, q;
                if (!items_of(aArgs[0], p) || !items_of(aArgs[1], q))
                    return nullptr;
                if (p.size() != q.size())
                    return error("ValueError", "Inputs are not the same length");
                ref total{ make_int(0) };
                bool intEnabled = true, intInUse = false, fltEnabled = true, fltInUse = false;
                long long intTotal = 0;
                triple_length fltTotal{ 0.0, 0.0, 0.0 };
                for (std::size_t i = 0u; i <= p.size(); ++i)
                {
                    bool const finished = i == p.size();
                    object* const pi = finished ? nullptr : p[i].get();
                    object* const qi = finished ? nullptr : q[i].get();
                    if (intEnabled)
                    {
                        if (!finished && kind_of(pi) == NEOS_PY_INT && kind_of(qi) == NEOS_PY_INT && !is_big(pi) && !is_big(qi))
                        {
                            long long product = 0, sum = 0;
                            if (!multiply_overflow(int_value(pi), int_value(qi), &product) && !add_overflow(intTotal, product, &sum))
                            {
                                intTotal = sum;
                                intInUse = true;
                                continue;
                            }
                        }
                        intEnabled = false;
                        if (intInUse)
                        {
                            ref const term{ make_int(intTotal) };
                            total = ref{ binary(NEOS_PY_ADD, total.get(), term.get(), false) };
                            if (!total)
                                return nullptr;
                            intInUse = false;
                        }
                    }
                    if (fltEnabled)
                    {
                        if (!finished)
                        {
                            bool const pf = kind_of(pi) == NEOS_PY_FLOAT, qf = kind_of(qi) == NEOS_PY_FLOAT;
                            bool const pInt = kind_of(pi) == NEOS_PY_INT || kind_of(pi) == NEOS_PY_BOOL;
                            bool const qInt = kind_of(qi) == NEOS_PY_INT || kind_of(qi) == NEOS_PY_BOOL;
                            if (((pf && qf) || (pf && qInt) || (qf && pInt)) && !is_big(pi) && !is_big(qi))
                            {
                                double fp = 0.0, fq = 0.0;
                                to_double(pi, fp);
                                to_double(qi, fq);
                                auto const next = tl_fma(fp, fq, fltTotal);
                                if (std::isfinite(next.hi))
                                {
                                    fltTotal = next;
                                    fltInUse = true;
                                    continue;
                                }
                            }
                        }
                        fltEnabled = false;
                        if (fltInUse)
                        {
                            ref const term{ make_float(tl_to_d(fltTotal)) };
                            total = ref{ binary(NEOS_PY_ADD, total.get(), term.get(), false) };
                            if (!total)
                                return nullptr;
                            fltInUse = false;
                        }
                    }
                    if (finished)
                        break;
                    ref const term{ binary(NEOS_PY_MULTIPLY, pi, qi, false) };
                    if (!term)
                        return nullptr;
                    total = ref{ binary(NEOS_PY_ADD, total.get(), term.get(), false) };
                    if (!total)
                        return nullptr;
                }
                return total.release_ownership();
            }

            object* m_isclose(std::vector<object*> const& aArgs, object* aKwargs)
            {
                object* relTolObject = nullptr;
                object* absTolObject = nullptr;
                if (!keywords(aKwargs, { { "rel_tol", &relTolObject }, { "abs_tol", &absTolObject } }, "isclose") || !arity(aArgs, 2u, 2u, "isclose"))
                    return nullptr;
                double a = 0.0, b = 0.0, relTol = 1e-09, absTol = 0.0;
                if (!to_double(aArgs[0], a) || !to_double(aArgs[1], b) || (relTolObject != nullptr && !to_double(relTolObject, relTol)) ||
                    (absTolObject != nullptr && !to_double(absTolObject, absTol)))
                    return nullptr;
                if (relTol < 0.0 || absTol < 0.0)
                    return error("ValueError", "tolerances must be non-negative");
                if (a == b)
                    return make_bool(true);
                if (std::isinf(a) || std::isinf(b))
                    return make_bool(false);
                double const diff = std::fabs(b - a);
                return make_bool((diff <= std::fabs(relTol * b)) || (diff <= std::fabs(relTol * a)) || (diff <= absTol));
            }

            object* predicate(std::vector<object*> const& aArgs, char const* aName, bool (*aTest)(double))
            {
                if (!arity(aArgs, 1u, 1u, aName))
                    return nullptr;
                double x = 0.0;
                if (!arg_double(aArgs, 0, x))
                    return nullptr;
                return make_bool(aTest(x));
            }

            object* m_isnan(std::vector<object*> const& a, object*) { return predicate(a, "isnan", [](double x) { return std::isnan(x); }); }
            object* m_isinf(std::vector<object*> const& a, object*) { return predicate(a, "isinf", [](double x) { return std::isinf(x); }); }
            object* m_isfinite(std::vector<object*> const& a, object*) { return predicate(a, "isfinite", [](double x) { return std::isfinite(x); }); }

            // (an operation on ints (native: see binary); null if it raised)
            ref int_operation(int aOperation, ref const& a, ref const& b)
            {
                if (!a || !b)
                    return ref{};
                return ref{ binary(aOperation, a.get(), b.get(), false) };
            }

            // the product of the integers from aFirst to aLast (binary splitting: the factors' sizes balanced)
            ref range_product(long long aFirst, long long aLast)
            {
                if (aLast - aFirst < 16)
                {
                    ref result{ make_int(1) };
                    long long chunk = 1;
                    for (long long i = aFirst; i <= aLast; ++i)
                    {
                        long long product = 0;
                        if (multiply_overflow(chunk, i, &product))
                        {
                            result = int_operation(NEOS_PY_MULTIPLY, result, ref{ make_int(chunk) });
                            chunk = i;
                        }
                        else
                            chunk = product;
                    }
                    return int_operation(NEOS_PY_MULTIPLY, result, ref{ make_int(chunk) });
                }
                auto const middle = aFirst + (aLast - aFirst) / 2;
                return int_operation(NEOS_PY_MULTIPLY, range_product(aFirst, middle), range_product(middle + 1, aLast));
            }

            object* m_factorial(std::vector<object*> const& aArgs, object*)
            {
                if (!arity(aArgs, 1u, 1u, "factorial"))
                    return nullptr;
                if (is_big(aArgs[0]) && !big_negative(big_of(aArgs[0])))
                    return error("OverflowError", "factorial() argument should not exceed 9223372036854775807");
                long long n = 0;
                if (!is_big(aArgs[0]) && !arg_index(aArgs, 0, n))
                    return nullptr;
                if (n < 0 || is_big(aArgs[0]))
                    return error("ValueError", "factorial() not defined for negative values");
                if (n < 2)
                    return make_int(1);
                return range_product(2, n).release_ownership();
            }

            object* m_isqrt(std::vector<object*> const& aArgs, object*)
            {
                if (!arity(aArgs, 1u, 1u, "isqrt"))
                    return nullptr;
                long long n = 0;
                if (is_big(aArgs[0]))
                {
                    if (limb_view view; limbs_of(aArgs[0], view))
                    {
                        if (view.negative)
                            return error("ValueError", "isqrt() argument must be nonnegative");
                        // (as CPython's (Modules/mathmodule.c, Python Software Foundation License): a = isqrt(n >> 2 (c - d)) to d 
                        // bits, d doubling)
                        ref const n = ref::borrowed(aArgs[0]);
                        auto const c = static_cast<long long>((limbs_bits(view.data, view.size) - 1u) / 2u);
                        ref a{ make_int(1) };
                        long long d = 0;
                        for (auto shift = static_cast<int>(std::bit_width(static_cast<unsigned long long>(c))); shift-- > 0;)
                        {
                            auto const e = d;
                            d = c >> shift;
                            auto const high = int_operation(NEOS_PY_LSHIFT, a, ref{ make_int(d - e - 1) });
                            auto const low = int_operation(NEOS_PY_FLOOR_DIVIDE, int_operation(NEOS_PY_RSHIFT, n, ref{ make_int(2 * c - e - d + 1) }), a);
                            a = int_operation(NEOS_PY_ADD, high, low);
                        }
                        auto const square = int_operation(NEOS_PY_MULTIPLY, a, a);
                        if (!square)
                            return nullptr;
                        if (limbs_order(square.get(), n.get()) > 0)
                            a = int_operation(NEOS_PY_SUBTRACT, a, ref{ make_int(1) });
                        return a.release_ownership();
                    }
                    auto const& v = big_of(aArgs[0]);
                    if (big_negative(v))
                        return error("ValueError", "isqrt() argument must be nonnegative");
                    // (Newton's method from above)
                    auto const bits = big_bit_length(v);
                    big_integer x = big_integer{ 1 } << static_cast<std::int64_t>((bits + 1u) / 2u);
                    for (;;)
                    {
                        auto const y = (x + v / x) >> std::int64_t{ 1 };
                        if (!(y < x))
                            break;
                        x = y;
                    }
                    return make_int(std::move(x));
                }
                if (!arg_index(aArgs, 0, n))
                    return nullptr;
                if (n < 0)
                    return error("ValueError", "isqrt() argument must be nonnegative");
                auto r = static_cast<unsigned long long>(std::sqrt(static_cast<double>(n)));
                while (r * r > static_cast<unsigned long long>(n))
                    --r;
                while ((r + 1u) * (r + 1u) <= static_cast<unsigned long long>(n))
                    ++r;
                return make_int(static_cast<long long>(r));
            }

            big_integer big_gcd(big_integer a, big_integer b)
            {
                a = big_abs(a);
                b = big_abs(b);
                while (!big_zero(b))
                {
                    auto const t = a % b;
                    a = b;
                    b = t;
                }
                return a;
            }

            long long gcd_of(long long a, long long b)
            {
                a = a < 0 ? -a : a;
                b = b < 0 ? -b : b;
                while (b != 0)
                {
                    auto const t = a % b;
                    a = b;
                    b = t;
                }
                return a;
            }

            // (an int, or an object with __index__)
            big_integer index_value(object* aObject)
            {
                if (is_int_like(aObject))
                    return big_value(aObject);
                long long v = 0;
                to_index(aObject, v);
                return big_integer{ v };
            }

            // (the arguments must be ints)
            bool int_arguments(std::vector<object*> const& aArgs)
            {
                for (auto arg : aArgs)
                    if (!is_int_like(arg))
                    {
                        long long ignored = 0;
                        if (!to_index(arg, ignored))
                            return false;
                    }
                return true;
            }

            // (an int, or an object with __index__, as an int with limbs (see limbs_of))
            ref index_int(object* aObject)
            {
                if (limb_view v; limbs_of(aObject, v))
                    return ref::borrowed(aObject);
                return ref{ make_int(index_value(aObject)) };
            }

            ref int_gcd(ref const& a, ref const& b)
            {
                limb_view x, y;
                if (!limbs_of(a.get(), x) || !limbs_of(b.get(), y))
                    return ref{ make_int(big_gcd(big_value(a.get()), big_value(b.get()))) };
                return ref{ gcd_views(x, y) };
            }

            object* m_gcd(std::vector<object*> const& aArgs, object*)
            {
                if (!int_arguments(aArgs))
                    return nullptr;
                ref r{ make_int(0) };
                for (auto arg : aArgs)
                    r = int_gcd(r, index_int(arg));
                return r.release_ownership();
            }

            object* m_lcm(std::vector<object*> const& aArgs, object*)
            {
                if (!int_arguments(aArgs))
                    return nullptr;
                ref r{ make_int(1) };
                for (auto arg : aArgs)
                {
                    auto const v = index_int(arg);
                    if (truth(r.get()) == 0 || truth(v.get()) == 0)
                    {
                        r = ref{ make_int(0) };
                        continue;
                    }
                    auto const product = int_operation(NEOS_PY_MULTIPLY, int_operation(NEOS_PY_FLOOR_DIVIDE, r, int_gcd(r, v)), v);
                    if (!product)
                        return nullptr;
                    r = ref{ unary(NEOS_PY_ABSOLUTE, product.get()) };
                }
                return r.release_ownership();
            }

            object* combinatorial(std::vector<object*> const& aArgs, char const* aName, bool aComb)
            {
                if (!arity(aArgs, aComb ? 2u : 1u, 2u, aName))
                    return nullptr;
                bool const all = aArgs.size() == 1u || kind_of(aArgs[1]) == NEOS_PY_NONE;
                if (!int_arguments(all ? std::vector<object*>{ aArgs[0] } : std::vector<object*>{ aArgs[0], aArgs[1] }))
                    return nullptr;
                auto const n = index_value(aArgs[0]);
                auto k = all ? n : index_value(aArgs[1]);
                if (big_negative(n))
                    return error("ValueError", "n must be a non-negative integer");
                if (big_negative(k))
                    return error("ValueError", "k must be a non-negative integer");
                if (n < k)
                    return make_int(0);
                if (aComb && n - k < k)
                    k = n - k;
                if (big_detail::magnitude(k) > 1u)
                    return error("OverflowError", std::string{ aComb ? "min(n - k, k)" : "k" } + " must not exceed 9223372036854775807");
                auto const count = static_cast<long long>(big_detail::cword(k, 0u));
                // (perm: n (n - 1) ... (n - k + 1); comb: each partial product of i factors is divisible by i!)
                ref const nObject{ make_int(n) };
                ref r{ make_int(1) };
                for (long long i = 0; i < count && r; ++i)
                {
                    r = int_operation(NEOS_PY_MULTIPLY, r, int_operation(NEOS_PY_SUBTRACT, nObject, ref{ make_int(i) }));
                    if (aComb)
                        r = int_operation(NEOS_PY_FLOOR_DIVIDE, r, ref{ make_int(i + 1) });
                }
                return r.release_ownership();
            }

            object* m_comb(std::vector<object*> const& a, object*) { return combinatorial(a, "comb", true); }
            object* m_perm(std::vector<object*> const& a, object*) { return combinatorial(a, "perm", false); }

            object* m_nextafter(std::vector<object*> const& aArgs, object* aKwargs)
            {
                object* steps = nullptr;
                if (!keywords(aKwargs, { { "steps", &steps } }, "nextafter") || !arity(aArgs, 2u, 2u, "nextafter"))
                    return nullptr;
                double x = 0.0, y = 0.0;
                if (!arg_double(aArgs, 0, x) || !arg_double(aArgs, 1, y))
                    return nullptr;
                if (steps == nullptr || kind_of(steps) == NEOS_PY_NONE)
                    return make_float(std::nextafter(x, y));
                long long n = 0;
                if (!to_index(steps, n))
                    return nullptr;
                if (n < 0)
                    return error("ValueError", "steps must be a non-negative integer");
                for (long long i = 0; i < n && x != y && !std::isnan(x) && !std::isnan(y); ++i)
                    x = std::nextafter(x, y);
                if (n > 0 && std::isnan(y))
                    x = y;
                return make_float(x);
            }

            object* m_ulp(std::vector<object*> const& aArgs, object*)
            {
                if (!arity(aArgs, 1u, 1u, "ulp"))
                    return nullptr;
                double x = 0.0;
                if (!arg_double(aArgs, 0, x))
                    return nullptr;
                if (std::isnan(x))
                    return make_float(x);
                x = std::fabs(x);
                if (std::isinf(x))
                    return make_float(x);
                double const x2 = std::nextafter(x, std::numeric_limits<double>::infinity());
                if (std::isinf(x2))
                    return make_float(x - std::nextafter(x, -std::numeric_limits<double>::infinity()));
                return make_float(x2 - x);
            }

            builtin_entry const sFunctions[] =
            {
                { "acos", m_acos }, { "acosh", m_acosh }, { "asin", m_asin }, { "asinh", m_asinh }, { "atan", m_atan }, { "atan2", m_atan2_ },
                { "atanh", m_atanh }, { "cbrt", m_cbrt }, { "ceil", m_ceil }, { "comb", m_comb }, { "copysign", m_copysign }, { "cos", m_cos },
                { "cosh", m_cosh }, { "degrees", m_degrees }, { "dist", m_dist }, { "erf", m_erf }, { "erfc", m_erfc }, { "exp", m_exp },
                { "exp2", m_exp2 }, { "expm1", m_expm1 }, { "fabs", m_fabs }, { "factorial", m_factorial }, { "floor", m_floor }, { "fmod", m_fmod },
                { "frexp", m_frexp }, { "fsum", m_fsum }, { "gamma", m_gamma }, { "gcd", m_gcd }, { "hypot", m_hypot }, { "isclose", m_isclose },
                { "isfinite", m_isfinite }, { "isinf", m_isinf }, { "isnan", m_isnan }, { "isqrt", m_isqrt }, { "lcm", m_lcm }, { "ldexp", m_ldexp },
                { "lgamma", m_lgamma_ }, { "log", m_log_ }, { "log10", m_log10_ }, { "log1p", m_log1p_ }, { "log2", m_log2_ }, { "modf", m_modf },
                { "nextafter", m_nextafter }, { "perm", m_perm }, { "pow", m_pow }, { "prod", m_prod }, { "radians", m_radians },
                { "remainder", m_remainder_ }, { "sin", m_sin }, { "sinh", m_sinh }, { "sqrt", m_sqrt }, { "sumprod", m_sumprod }, { "tan", m_tan },
                { "tanh", m_tanh }, { "trunc", m_trunc }, { "ulp", m_ulp }
            };
        }

        // (cmath: the algorithms of CPython's Modules/cmathmodule.c (Python Software Foundation License))
        namespace cmath_module
        {
            struct pc { double real; double imag; };
            enum { ok = 0, edom = 1, erange = 2 };
            thread_local int sErrno = ok;

            double constexpr PI = 3.141592653589793238462643383279502884197;
            double constexpr E = 2.718281828459045235360287471352662497757;
            double constexpr LN2 = 0.6931471805599453094;
            double constexpr LN10 = 2.302585092994045684;
            double const LARGE_DOUBLE = std::numeric_limits<double>::max() / 4.0;
            double const SQRT_LARGE_DOUBLE = std::sqrt(LARGE_DOUBLE);
            double const LOG_LARGE_DOUBLE = std::log(LARGE_DOUBLE);
            double const SQRT_DBL_MIN = std::sqrt(std::numeric_limits<double>::min());
            int constexpr SCALE_UP = 2 * (std::numeric_limits<double>::digits / 2) + 1;
            int constexpr SCALE_DOWN = -(SCALE_UP + 1) / 2;
            double constexpr INF = std::numeric_limits<double>::infinity();
            double constexpr N = std::numeric_limits<double>::quiet_NaN();
            double constexpr U = -9.5426319407711027e33; // (unlikely value, used as placeholder)
            double constexpr P = PI, P14 = 0.25 * PI, P12 = 0.5 * PI, P34 = 0.75 * PI;

            enum special_types { ST_NINF, ST_NEG, ST_NZERO, ST_PZERO, ST_POS, ST_PINF, ST_NAN };

            special_types special_type(double d)
            {
                if (std::isfinite(d))
                {
                    if (d != 0)
                        return std::copysign(1., d) == 1. ? ST_POS : ST_NEG;
                    return std::copysign(1., d) == 1. ? ST_PZERO : ST_NZERO;
                }
                if (std::isnan(d))
                    return ST_NAN;
                return std::copysign(1., d) == 1. ? ST_PINF : ST_NINF;
            }

            using table = pc[7][7];

            table const acos_special_values = {
              { {P34,INF}, {P,INF},  {P,INF},  {P,-INF},  {P,-INF},  {P34,-INF}, {N,INF} },
              { {P12,INF}, {U,U},    {U,U},    {U,U},     {U,U},     {P12,-INF}, {N,N} },
              { {P12,INF}, {U,U},    {P12,0.}, {P12,-0.}, {U,U},     {P12,-INF}, {P12,N} },
              { {P12,INF}, {U,U},    {P12,0.}, {P12,-0.}, {U,U},     {P12,-INF}, {P12,N} },
              { {P12,INF}, {U,U},    {U,U},    {U,U},     {U,U},     {P12,-INF}, {N,N} },
              { {P14,INF}, {0.,INF}, {0.,INF}, {0.,-INF}, {0.,-INF}, {P14,-INF}, {N,INF} },
              { {N,INF},   {N,N},    {N,N},    {N,N},     {N,N},     {N,-INF},   {N,N} } };
            table const acosh_special_values = {
              { {INF,-P34}, {INF,-P},  {INF,-P},  {INF,P},  {INF,P},  {INF,P34}, {INF,N} },
              { {INF,-P12}, {U,U},     {U,U},     {U,U},    {U,U},    {INF,P12}, {N,N} },
              { {INF,-P12}, {U,U},     {0.,-P12}, {0.,P12}, {U,U},    {INF,P12}, {N,N} },
              { {INF,-P12}, {U,U},     {0.,-P12}, {0.,P12}, {U,U},    {INF,P12}, {N,N} },
              { {INF,-P12}, {U,U},     {U,U},     {U,U},    {U,U},    {INF,P12}, {N,N} },
              { {INF,-P14}, {INF,-0.}, {INF,-0.}, {INF,0.}, {INF,0.}, {INF,P14}, {INF,N} },
              { {INF,N},    {N,N},     {N,N},     {N,N},    {N,N},    {INF,N},   {N,N} } };
            table const asinh_special_values = {
              { {-INF,-P14}, {-INF,-0.}, {-INF,-0.}, {-INF,0.}, {-INF,0.}, {-INF,P14}, {-INF,N} },
              { {-INF,-P12}, {U,U},      {U,U},      {U,U},     {U,U},     {-INF,P12}, {N,N} },
              { {-INF,-P12}, {U,U},      {-0.,-0.},  {-0.,0.},  {U,U},     {-INF,P12}, {N,N} },
              { {INF,-P12},  {U,U},      {0.,-0.},   {0.,0.},   {U,U},     {INF,P12},  {N,N} },
              { {INF,-P12},  {U,U},      {U,U},      {U,U},     {U,U},     {INF,P12},  {N,N} },
              { {INF,-P14},  {INF,-0.},  {INF,-0.},  {INF,0.},  {INF,0.},  {INF,P14},  {INF,N} },
              { {INF,N},     {N,N},      {N,-0.},    {N,0.},    {N,N},     {INF,N},    {N,N} } };
            table const atanh_special_values = {
              { {-0.,-P12}, {-0.,-P12}, {-0.,-P12}, {-0.,P12}, {-0.,P12}, {-0.,P12}, {-0.,N} },
              { {-0.,-P12}, {U,U},      {U,U},      {U,U},     {U,U},     {-0.,P12}, {N,N} },
              { {-0.,-P12}, {U,U},      {-0.,-0.},  {-0.,0.},  {U,U},     {-0.,P12}, {-0.,N} },
              { {0.,-P12},  {U,U},      {0.,-0.},   {0.,0.},   {U,U},     {0.,P12},  {0.,N} },
              { {0.,-P12},  {U,U},      {U,U},      {U,U},     {U,U},     {0.,P12},  {N,N} },
              { {0.,-P12},  {0.,-P12},  {0.,-P12},  {0.,P12},  {0.,P12},  {0.,P12},  {0.,N} },
              { {0.,-P12},  {N,N},      {N,N},      {N,N},     {N,N},     {0.,P12},  {N,N} } };
            table const cosh_special_values = {
              { {INF,N}, {U,U}, {INF,0.},  {INF,-0.}, {U,U}, {INF,N}, {INF,N} },
              { {N,N},   {U,U}, {U,U},     {U,U},     {U,U}, {N,N},   {N,N} },
              { {N,0.},  {U,U}, {1.,0.},   {1.,-0.},  {U,U}, {N,0.},  {N,0.} },
              { {N,0.},  {U,U}, {1.,-0.},  {1.,0.},   {U,U}, {N,0.},  {N,0.} },
              { {N,N},   {U,U}, {U,U},     {U,U},     {U,U}, {N,N},   {N,N} },
              { {INF,N}, {U,U}, {INF,-0.}, {INF,0.},  {U,U}, {INF,N}, {INF,N} },
              { {N,N},   {N,N}, {N,0.},    {N,0.},    {N,N}, {N,N},   {N,N} } };
            table const exp_special_values = {
              { {0.,0.}, {U,U}, {0.,-0.},  {0.,0.},  {U,U}, {0.,0.}, {0.,0.} },
              { {N,N},   {U,U}, {U,U},     {U,U},    {U,U}, {N,N},   {N,N} },
              { {N,N},   {U,U}, {1.,-0.},  {1.,0.},  {U,U}, {N,N},   {N,N} },
              { {N,N},   {U,U}, {1.,-0.},  {1.,0.},  {U,U}, {N,N},   {N,N} },
              { {N,N},   {U,U}, {U,U},     {U,U},    {U,U}, {N,N},   {N,N} },
              { {INF,N}, {U,U}, {INF,-0.}, {INF,0.}, {U,U}, {INF,N}, {INF,N} },
              { {N,N},   {N,N}, {N,-0.},   {N,0.},   {N,N}, {N,N},   {N,N} } };
            table const log_special_values = {
              { {INF,-P34}, {INF,-P},  {INF,-P},   {INF,P},   {INF,P},  {INF,P34},  {INF,N} },
              { {INF,-P12}, {U,U},     {U,U},      {U,U},     {U,U},    {INF,P12},  {N,N} },
              { {INF,-P12}, {U,U},     {-INF,-P},  {-INF,P},  {U,U},    {INF,P12},  {N,N} },
              { {INF,-P12}, {U,U},     {-INF,-0.}, {-INF,0.}, {U,U},    {INF,P12},  {N,N} },
              { {INF,-P12}, {U,U},     {U,U},      {U,U},     {U,U},    {INF,P12},  {N,N} },
              { {INF,-P14}, {INF,-0.}, {INF,-0.},  {INF,0.},  {INF,0.}, {INF,P14},  {INF,N} },
              { {INF,N},    {N,N},     {N,N},      {N,N},     {N,N},    {INF,N},    {N,N} } };
            table const sinh_special_values = {
              { {INF,N}, {U,U}, {-INF,-0.}, {-INF,0.}, {U,U}, {INF,N}, {INF,N} },
              { {N,N},   {U,U}, {U,U},      {U,U},     {U,U}, {N,N},   {N,N} },
              { {0.,N},  {U,U}, {-0.,-0.},  {-0.,0.},  {U,U}, {0.,N},  {0.,N} },
              { {0.,N},  {U,U}, {0.,-0.},   {0.,0.},   {U,U}, {0.,N},  {0.,N} },
              { {N,N},   {U,U}, {U,U},      {U,U},     {U,U}, {N,N},   {N,N} },
              { {INF,N}, {U,U}, {INF,-0.},  {INF,0.},  {U,U}, {INF,N}, {INF,N} },
              { {N,N},   {N,N}, {N,-0.},    {N,0.},    {N,N}, {N,N},   {N,N} } };
            table const sqrt_special_values = {
              { {INF,-INF}, {0.,-INF}, {0.,-INF}, {0.,INF}, {0.,INF}, {INF,INF}, {N,INF} },
              { {INF,-INF}, {U,U},     {U,U},     {U,U},    {U,U},    {INF,INF}, {N,N} },
              { {INF,-INF}, {U,U},     {0.,-0.},  {0.,0.},  {U,U},    {INF,INF}, {N,N} },
              { {INF,-INF}, {U,U},     {0.,-0.},  {0.,0.},  {U,U},    {INF,INF}, {N,N} },
              { {INF,-INF}, {U,U},     {U,U},     {U,U},    {U,U},    {INF,INF}, {N,N} },
              { {INF,-INF}, {INF,-0.}, {INF,-0.}, {INF,0.}, {INF,0.}, {INF,INF}, {INF,N} },
              { {INF,-INF}, {N,N},     {N,N},     {N,N},    {N,N},    {INF,INF}, {N,N} } };
            table const tanh_special_values = {
              { {-1.,0.}, {U,U}, {-1.,-0.}, {-1.,0.}, {U,U}, {-1.,0.}, {-1.,0.} },
              { {N,N},    {U,U}, {U,U},     {U,U},    {U,U}, {N,N},    {N,N} },
              { {N,N},    {U,U}, {-0.,-0.}, {-0.,0.}, {U,U}, {N,N},    {N,N} },
              { {N,N},    {U,U}, {0.,-0.},  {0.,0.},  {U,U}, {N,N},    {N,N} },
              { {N,N},    {U,U}, {U,U},     {U,U},    {U,U}, {N,N},    {N,N} },
              { {1.,0.},  {U,U}, {1.,-0.},  {1.,0.},  {U,U}, {1.,0.},  {1.,0.} },
              { {N,N},    {N,N}, {N,-0.},   {N,0.},   {N,N}, {N,N},    {N,N} } };
            table const rect_special_values = {
              { {INF,N}, {U,U}, {-INF,0.}, {-INF,-0.}, {U,U}, {INF,N}, {INF,N} },
              { {N,N},   {U,U}, {U,U},     {U,U},      {U,U}, {N,N},   {N,N} },
              { {0.,0.}, {U,U}, {-0.,0.},  {-0.,-0.},  {U,U}, {0.,0.}, {0.,0.} },
              { {0.,0.}, {U,U}, {0.,-0.},  {0.,0.},    {U,U}, {0.,0.}, {0.,0.} },
              { {N,N},   {U,U}, {U,U},     {U,U},      {U,U}, {N,N},   {N,N} },
              { {INF,N}, {U,U}, {INF,-0.}, {INF,0.},   {U,U}, {INF,N}, {INF,N} },
              { {N,N},   {N,N}, {N,0.},    {N,0.},     {N,N}, {N,N},   {N,N} } };

            bool special(pc z, table const& aTable, pc& aResult)
            {
                if (!std::isfinite(z.real) || !std::isfinite(z.imag))
                {
                    sErrno = ok;
                    aResult = aTable[special_type(z.real)][special_type(z.imag)];
                    return true;
                }
                return false;
            }

            pc neg(pc z) { return { -z.real, -z.imag }; }

            pc c_sqrt(pc z)
            {
                pc r;
                if (special(z, sqrt_special_values, r))
                    return r;
                if (z.real == 0. && z.imag == 0.)
                {
                    sErrno = ok;
                    return { 0., z.imag };
                }
                double ax = std::fabs(z.real), ay = std::fabs(z.imag), s = 0.0;
                if (ax < std::numeric_limits<double>::min() && ay < std::numeric_limits<double>::min())
                {
                    ax = std::ldexp(ax, SCALE_UP);
                    s = std::ldexp(std::sqrt(ax + std::hypot(ax, std::ldexp(ay, SCALE_UP))), SCALE_DOWN);
                }
                else
                {
                    ax /= 8.;
                    s = 2. * std::sqrt(ax + std::hypot(ax, ay / 8.));
                }
                double const d = ay / (2. * s);
                if (z.real >= 0.)
                    r = { s, std::copysign(d, z.imag) };
                else
                    r = { d, std::copysign(s, z.imag) };
                sErrno = ok;
                return r;
            }

            pc c_acos(pc z)
            {
                pc r;
                if (special(z, acos_special_values, r))
                    return r;
                if (std::fabs(z.real) > LARGE_DOUBLE || std::fabs(z.imag) > LARGE_DOUBLE)
                {
                    r.real = std::atan2(std::fabs(z.imag), z.real);
                    if (z.real < 0.)
                        r.imag = -std::copysign(std::log(std::hypot(z.real / 2., z.imag / 2.)) + LN2 * 2., z.imag);
                    else
                        r.imag = std::copysign(std::log(std::hypot(z.real / 2., z.imag / 2.)) + LN2 * 2., -z.imag);
                }
                else
                {
                    auto const s1 = c_sqrt({ 1. - z.real, -z.imag });
                    auto const s2 = c_sqrt({ 1. + z.real, z.imag });
                    r.real = 2. * std::atan2(s1.real, s2.real);
                    r.imag = std::asinh(s2.real * s1.imag - s2.imag * s1.real);
                }
                sErrno = ok;
                return r;
            }

            pc c_acosh(pc z)
            {
                pc r;
                if (special(z, acosh_special_values, r))
                    return r;
                if (std::fabs(z.real) > LARGE_DOUBLE || std::fabs(z.imag) > LARGE_DOUBLE)
                {
                    r.real = std::log(std::hypot(z.real / 2., z.imag / 2.)) + LN2 * 2.;
                    r.imag = std::atan2(z.imag, z.real);
                }
                else
                {
                    auto const s1 = c_sqrt({ z.real - 1., z.imag });
                    auto const s2 = c_sqrt({ z.real + 1., z.imag });
                    r.real = std::asinh(s1.real * s2.real + s1.imag * s2.imag);
                    r.imag = 2. * std::atan2(s1.imag, s2.real);
                }
                sErrno = ok;
                return r;
            }

            pc c_asinh(pc z)
            {
                pc r;
                if (special(z, asinh_special_values, r))
                    return r;
                if (std::fabs(z.real) > LARGE_DOUBLE || std::fabs(z.imag) > LARGE_DOUBLE)
                {
                    if (z.imag >= 0.)
                        r.real = std::copysign(std::log(std::hypot(z.real / 2., z.imag / 2.)) + LN2 * 2., z.real);
                    else
                        r.real = -std::copysign(std::log(std::hypot(z.real / 2., z.imag / 2.)) + LN2 * 2., -z.real);
                    r.imag = std::atan2(z.imag, std::fabs(z.real));
                }
                else
                {
                    auto const s1 = c_sqrt({ 1. + z.imag, -z.real });
                    auto const s2 = c_sqrt({ 1. - z.imag, z.real });
                    r.real = std::asinh(s1.real * s2.imag - s2.real * s1.imag);
                    r.imag = std::atan2(z.imag, s1.real * s2.real - s1.imag * s2.imag);
                }
                sErrno = ok;
                return r;
            }

            pc c_asin(pc z)
            {
                // (asin(z) = -i asinh(iz))
                auto const s = c_asinh({ -z.imag, z.real });
                return { s.imag, -s.real };
            }

            pc c_atanh(pc z)
            {
                pc r;
                if (special(z, atanh_special_values, r))
                    return r;
                if (z.real < 0.)
                    return neg(c_atanh(neg(z)));
                double const ay = std::fabs(z.imag);
                if (z.real > SQRT_LARGE_DOUBLE || ay > SQRT_LARGE_DOUBLE)
                {
                    double const h = std::hypot(z.real / 2., z.imag / 2.);
                    r.real = z.real / 4. / h / h;
                    r.imag = -std::copysign(PI / 2., -z.imag);
                    sErrno = ok;
                }
                else if (z.real == 1. && ay < SQRT_DBL_MIN)
                {
                    if (ay == 0.)
                    {
                        r.real = INF;
                        r.imag = z.imag;
                        sErrno = edom;
                    }
                    else
                    {
                        r.real = -std::log(std::sqrt(ay) / std::sqrt(std::hypot(ay, 2.)));
                        r.imag = std::copysign(std::atan2(2., -ay) / 2, z.imag);
                        sErrno = ok;
                    }
                }
                else
                {
                    r.real = std::log1p(4. * z.real / ((1 - z.real) * (1 - z.real) + ay * ay)) / 4.;
                    r.imag = -std::atan2(-2. * z.imag, (1 - z.real) * (1 + z.real) - ay * ay) / 2.;
                    sErrno = ok;
                }
                return r;
            }

            pc c_atan(pc z)
            {
                // (atan(z) = -i atanh(iz))
                auto const s = c_atanh({ -z.imag, z.real });
                return { s.imag, -s.real };
            }

            double c_atan2(pc z)
            {
                if (std::isnan(z.real) || std::isnan(z.imag))
                    return N;
                if (std::isinf(z.imag))
                {
                    if (std::isinf(z.real))
                        return std::copysign(std::copysign(1., z.real) == 1. ? 0.25 * PI : 0.75 * PI, z.imag);
                    return std::copysign(0.5 * PI, z.imag);
                }
                if (std::isinf(z.real) || z.imag == 0.)
                    return std::copysign(std::copysign(1., z.real) == 1. ? 0. : PI, z.imag);
                return std::atan2(z.imag, z.real);
            }

            pc c_cosh(pc z)
            {
                pc r;
                if (!std::isfinite(z.real) || !std::isfinite(z.imag))
                {
                    if (std::isinf(z.real) && std::isfinite(z.imag) && z.imag != 0.)
                    {
                        if (z.real > 0)
                            r = { std::copysign(INF, std::cos(z.imag)), std::copysign(INF, std::sin(z.imag)) };
                        else
                            r = { std::copysign(INF, std::cos(z.imag)), -std::copysign(INF, std::sin(z.imag)) };
                    }
                    else
                        r = cosh_special_values[special_type(z.real)][special_type(z.imag)];
                    sErrno = std::isinf(z.imag) && !std::isnan(z.real) ? edom : ok;
                    return r;
                }
                if (std::fabs(z.real) > LOG_LARGE_DOUBLE)
                {
                    double const x_minus_one = z.real - std::copysign(1., z.real);
                    r = { std::cos(z.imag) * std::cosh(x_minus_one) * E, std::sin(z.imag) * std::sinh(x_minus_one) * E };
                }
                else
                    r = { std::cos(z.imag) * std::cosh(z.real), std::sin(z.imag) * std::sinh(z.real) };
                sErrno = std::isinf(r.real) || std::isinf(r.imag) ? erange : ok;
                return r;
            }

            pc c_cos(pc z)
            {
                // (cos(z) = cosh(iz))
                return c_cosh({ -z.imag, z.real });
            }

            pc c_exp(pc z)
            {
                pc r;
                if (!std::isfinite(z.real) || !std::isfinite(z.imag))
                {
                    if (std::isinf(z.real) && std::isfinite(z.imag) && z.imag != 0.)
                    {
                        if (z.real > 0)
                            r = { std::copysign(INF, std::cos(z.imag)), std::copysign(INF, std::sin(z.imag)) };
                        else
                            r = { std::copysign(0., std::cos(z.imag)), std::copysign(0., std::sin(z.imag)) };
                    }
                    else
                        r = exp_special_values[special_type(z.real)][special_type(z.imag)];
                    sErrno = std::isinf(z.imag) && (std::isfinite(z.real) || (std::isinf(z.real) && z.real > 0)) ? edom : ok;
                    return r;
                }
                if (z.real > LOG_LARGE_DOUBLE)
                {
                    double const l = std::exp(z.real - 1.);
                    r = { l * std::cos(z.imag) * E, l * std::sin(z.imag) * E };
                }
                else
                {
                    double const l = std::exp(z.real);
                    r = { l * std::cos(z.imag), l * std::sin(z.imag) };
                }
                sErrno = std::isinf(r.real) || std::isinf(r.imag) ? erange : ok;
                return r;
            }

            pc c_log(pc z)
            {
                pc r;
                if (special(z, log_special_values, r))
                    return r;
                double const ax = std::fabs(z.real), ay = std::fabs(z.imag);
                double constexpr dblMin = std::numeric_limits<double>::min();
                int constexpr mantDig = std::numeric_limits<double>::digits;
                if (ax > LARGE_DOUBLE || ay > LARGE_DOUBLE)
                    r.real = std::log(std::hypot(ax / 2., ay / 2.)) + LN2;
                else if (ax < dblMin && ay < dblMin)
                {
                    if (ax > 0. || ay > 0.)
                        r.real = std::log(std::hypot(std::ldexp(ax, mantDig), std::ldexp(ay, mantDig))) - mantDig * LN2;
                    else
                    {
                        r.real = -INF;
                        r.imag = std::atan2(z.imag, z.real);
                        sErrno = edom;
                        return r;
                    }
                }
                else
                {
                    double const h = std::hypot(ax, ay);
                    if (0.71 <= h && h <= 1.73)
                    {
                        double const am = ax > ay ? ax : ay;
                        double const an = ax > ay ? ay : ax;
                        r.real = std::log1p((am - 1) * (am + 1) + an * an) / 2.;
                    }
                    else
                        r.real = std::log(h);
                }
                r.imag = std::atan2(z.imag, z.real);
                sErrno = ok;
                return r;
            }

            pc c_log10(pc z)
            {
                auto r = c_log(z);
                r.real = r.real / LN10;
                r.imag = r.imag / LN10;
                return r;
            }

            pc c_sinh(pc z)
            {
                pc r;
                if (!std::isfinite(z.real) || !std::isfinite(z.imag))
                {
                    if (std::isinf(z.real) && std::isfinite(z.imag) && z.imag != 0.)
                    {
                        if (z.real > 0)
                            r = { std::copysign(INF, std::cos(z.imag)), std::copysign(INF, std::sin(z.imag)) };
                        else
                            r = { -std::copysign(INF, std::cos(z.imag)), std::copysign(INF, std::sin(z.imag)) };
                    }
                    else
                        r = sinh_special_values[special_type(z.real)][special_type(z.imag)];
                    sErrno = std::isinf(z.imag) && !std::isnan(z.real) ? edom : ok;
                    return r;
                }
                if (std::fabs(z.real) > LOG_LARGE_DOUBLE)
                {
                    double const x_minus_one = z.real - std::copysign(1., z.real);
                    r = { std::cos(z.imag) * std::sinh(x_minus_one) * E, std::sin(z.imag) * std::cosh(x_minus_one) * E };
                }
                else
                    r = { std::cos(z.imag) * std::sinh(z.real), std::sin(z.imag) * std::cosh(z.real) };
                sErrno = std::isinf(r.real) || std::isinf(r.imag) ? erange : ok;
                return r;
            }

            pc c_sin(pc z)
            {
                // (sin(z) = -i sinh(iz))
                auto const s = c_sinh({ -z.imag, z.real });
                return { s.imag, -s.real };
            }

            pc c_tanh(pc z)
            {
                pc r;
                if (!std::isfinite(z.real) || !std::isfinite(z.imag))
                {
                    if (std::isinf(z.real) && std::isfinite(z.imag) && z.imag != 0.)
                        r = { z.real > 0 ? 1.0 : -1.0, std::copysign(0., 2. * std::sin(z.imag) * std::cos(z.imag)) };
                    else
                        r = tanh_special_values[special_type(z.real)][special_type(z.imag)];
                    sErrno = std::isinf(z.imag) && std::isfinite(z.real) ? edom : ok;
                    return r;
                }
                if (std::fabs(z.real) > LOG_LARGE_DOUBLE)
                {
                    r.real = std::copysign(1., z.real);
                    r.imag = 4. * std::sin(z.imag) * std::cos(z.imag) * std::exp(-2. * std::fabs(z.real));
                }
                else
                {
                    double const tx = std::tanh(z.real);
                    double const ty = std::tan(z.imag);
                    double const cx = 1. / std::cosh(z.real);
                    double const txty = tx * ty;
                    double const denom = 1. + txty * txty;
                    r.real = tx * (1. + ty * ty) / denom;
                    r.imag = ((ty / denom) * cx) * cx;
                }
                sErrno = ok;
                return r;
            }

            pc c_tan(pc z)
            {
                // (tan(z) = -i tanh(iz))
                auto const s = c_tanh({ -z.imag, z.real });
                return { s.imag, -s.real };
            }

            // (_Py_c_abs: errno ERANGE if it overflows)
            double c_abs(pc z)
            {
                if (!std::isfinite(z.real) || !std::isfinite(z.imag))
                {
                    sErrno = ok;
                    if (std::isinf(z.real))
                        return std::fabs(z.real);
                    if (std::isinf(z.imag))
                        return std::fabs(z.imag);
                    return N;
                }
                double const result = std::hypot(z.real, z.imag);
                sErrno = std::isfinite(result) ? ok : erange;
                return result;
            }

            // (_Py_c_quot: errno EDOM if the divisor is zero)
            pc c_quot_(pc a, pc b)
            {
                cpx r{};
                if (!c_quot({ a.real, a.imag }, { b.real, b.imag }, r))
                {
                    sErrno = edom;
                    return { 0.0, 0.0 };
                }
                return { r.real, r.imag };
            }

            object* math_error()
            {
                if (sErrno == edom)
                    return error("ValueError", "math domain error");
                return error("OverflowError", "math range error");
            }

            // (PyComplex_AsCComplex)
            bool arg_complex(object* aArg, pc& aResult)
            {
                if (kind_of(aArg) == NEOS_PY_COMPLEX)
                {
                    aResult = { as<complex_object>(aArg)->real, as<complex_object>(aArg)->imag };
                    return true;
                }
                aResult.imag = 0.0;
                return to_double(aArg, aResult.real);
            }

            bool one_argument(std::vector<object*> const& aArgs, char const* aName, pc& aResult)
            {
                if (aArgs.size() != 1u)
                {
                    error("TypeError", std::string{ "cmath." } + aName + "() takes exactly one argument (" + std::to_string(aArgs.size()) + " given)");
                    return false;
                }
                return arg_complex(aArgs[0], aResult);
            }

            template <pc (*Function)(pc)>
            object* function_1(std::vector<object*> const& aArgs, char const* aName)
            {
                pc z;
                if (!one_argument(aArgs, aName, z))
                    return nullptr;
                sErrno = ok;
                auto const r = Function(z);
                if (sErrno != ok)
                    return math_error();
                return make_complex(r.real, r.imag);
            }

            object* cm_acos(std::vector<object*> const& a, object*) { return function_1<c_acos>(a, "acos"); }
            object* cm_acosh(std::vector<object*> const& a, object*) { return function_1<c_acosh>(a, "acosh"); }
            object* cm_asin(std::vector<object*> const& a, object*) { return function_1<c_asin>(a, "asin"); }
            object* cm_asinh(std::vector<object*> const& a, object*) { return function_1<c_asinh>(a, "asinh"); }
            object* cm_atan(std::vector<object*> const& a, object*) { return function_1<c_atan>(a, "atan"); }
            object* cm_atanh(std::vector<object*> const& a, object*) { return function_1<c_atanh>(a, "atanh"); }
            object* cm_cos(std::vector<object*> const& a, object*) { return function_1<c_cos>(a, "cos"); }
            object* cm_cosh(std::vector<object*> const& a, object*) { return function_1<c_cosh>(a, "cosh"); }
            object* cm_exp(std::vector<object*> const& a, object*) { return function_1<c_exp>(a, "exp"); }
            object* cm_log10(std::vector<object*> const& a, object*) { return function_1<c_log10>(a, "log10"); }
            object* cm_sin(std::vector<object*> const& a, object*) { return function_1<c_sin>(a, "sin"); }
            object* cm_sinh(std::vector<object*> const& a, object*) { return function_1<c_sinh>(a, "sinh"); }
            object* cm_sqrt(std::vector<object*> const& a, object*) { return function_1<c_sqrt>(a, "sqrt"); }
            object* cm_tan(std::vector<object*> const& a, object*) { return function_1<c_tan>(a, "tan"); }
            object* cm_tanh(std::vector<object*> const& a, object*) { return function_1<c_tanh>(a, "tanh"); }

            object* cm_log(std::vector<object*> const& aArgs, object*)
            {
                if (aArgs.empty())
                    return error("TypeError", "log expected at least 1 argument, got 0");
                if (aArgs.size() > 2u)
                    return error("TypeError", "log expected at most 2 arguments, got " + std::to_string(aArgs.size()));
                pc x, y;
                if (!arg_complex(aArgs[0], x))
                    return nullptr;
                sErrno = ok;
                x = c_log(x);
                if (aArgs.size() == 2u)
                {
                    auto const savedErrno = sErrno;
                    if (!arg_complex(aArgs[1], y))
                        return nullptr;
                    y = c_log(y);
                    x = c_quot_(x, y);
                    if (sErrno == ok)
                        sErrno = savedErrno;
                }
                if (sErrno != ok)
                    return math_error();
                return make_complex(x.real, x.imag);
            }

            object* cm_phase(std::vector<object*> const& aArgs, object*)
            {
                pc z;
                if (!one_argument(aArgs, "phase", z))
                    return nullptr;
                return make_float(c_atan2(z));
            }

            object* cm_polar(std::vector<object*> const& aArgs, object*)
            {
                pc z;
                if (!one_argument(aArgs, "polar", z))
                    return nullptr;
                double const phi = c_atan2(z);
                sErrno = ok;
                double const r = c_abs(z);
                if (sErrno != ok)
                    return math_error();
                return make_tuple({ make_float(r), make_float(phi) });
            }

            object* cm_rect(std::vector<object*> const& aArgs, object*)
            {
                if (aArgs.size() != 2u)
                    return error("TypeError", "rect expected 2 arguments, got " + std::to_string(aArgs.size()));
                double r = 0.0, phi = 0.0;
                if (!to_double(aArgs[0], r) || !to_double(aArgs[1], phi))
                    return nullptr;
                pc z;
                sErrno = ok;
                if (!std::isfinite(r) || !std::isfinite(phi))
                {
                    if (std::isinf(r) && std::isfinite(phi) && phi != 0.)
                    {
                        if (r > 0)
                            z = { std::copysign(INF, std::cos(phi)), std::copysign(INF, std::sin(phi)) };
                        else
                            z = { -std::copysign(INF, std::cos(phi)), -std::copysign(INF, std::sin(phi)) };
                    }
                    else
                        z = rect_special_values[special_type(r)][special_type(phi)];
                    sErrno = r != 0. && !std::isnan(r) && std::isinf(phi) ? edom : ok;
                }
                else if (phi == 0.0)
                    z = { r, r * phi };
                else
                    z = { r * std::cos(phi), r * std::sin(phi) };
                if (sErrno != ok)
                    return math_error();
                return make_complex(z.real, z.imag);
            }

            object* cm_isfinite(std::vector<object*> const& aArgs, object*)
            {
                pc z;
                if (!one_argument(aArgs, "isfinite", z))
                    return nullptr;
                return make_bool(std::isfinite(z.real) && std::isfinite(z.imag));
            }

            object* cm_isnan(std::vector<object*> const& aArgs, object*)
            {
                pc z;
                if (!one_argument(aArgs, "isnan", z))
                    return nullptr;
                return make_bool(std::isnan(z.real) || std::isnan(z.imag));
            }

            object* cm_isinf(std::vector<object*> const& aArgs, object*)
            {
                pc z;
                if (!one_argument(aArgs, "isinf", z))
                    return nullptr;
                return make_bool(std::isinf(z.real) || std::isinf(z.imag));
            }

            object* cm_isclose(std::vector<object*> const& aArgs, object* aKwargs)
            {
                object* relTolObject = nullptr;
                object* absTolObject = nullptr;
                if (!keywords(aKwargs, { { "rel_tol", &relTolObject }, { "abs_tol", &absTolObject } }, "isclose"))
                    return nullptr;
                if (aArgs.size() != 2u)
                    return error("TypeError", "isclose() takes exactly 2 positional arguments (" + std::to_string(aArgs.size()) + " given)");
                pc a, b;
                double relTol = 1e-09, absTol = 0.0;
                if (!arg_complex(aArgs[0], a) || !arg_complex(aArgs[1], b) || (relTolObject != nullptr && !to_double(relTolObject, relTol)) ||
                    (absTolObject != nullptr && !to_double(absTolObject, absTol)))
                    return nullptr;
                if (relTol < 0.0 || absTol < 0.0)
                    return error("ValueError", "tolerances must be non-negative");
                if (a.real == b.real && a.imag == b.imag)
                    return make_bool(true);
                if (std::isinf(a.real) || std::isinf(a.imag) || std::isinf(b.real) || std::isinf(b.imag))
                    return make_bool(false);
                double const diff = c_abs({ a.real - b.real, a.imag - b.imag });
                return make_bool((diff <= relTol * c_abs(b)) || (diff <= relTol * c_abs(a)) || (diff <= absTol));
            }

            builtin_entry const sFunctions[] =
            {
                { "acos", cm_acos }, { "acosh", cm_acosh }, { "asin", cm_asin }, { "asinh", cm_asinh }, { "atan", cm_atan }, { "atanh", cm_atanh },
                { "cos", cm_cos }, { "cosh", cm_cosh }, { "exp", cm_exp }, { "isclose", cm_isclose }, { "isfinite", cm_isfinite },
                { "isinf", cm_isinf }, { "isnan", cm_isnan }, { "log", cm_log }, { "log10", cm_log10 }, { "phase", cm_phase },
                { "polar", cm_polar }, { "rect", cm_rect }, { "sin", cm_sin }, { "sinh", cm_sinh }, { "sqrt", cm_sqrt }, { "tan", cm_tan },
                { "tanh", cm_tanh }
            };
        }

        // sys.set_int_max_str_digits, sys.get_int_max_str_digits
        object* sys_set_int_max_str_digits(std::vector<object*> const& aArgs, object* aKwargs)
        {
            object* maxdigits = nullptr;
            if (!keywords(aKwargs, { { "maxdigits", &maxdigits } }, "set_int_max_str_digits") || !arity(aArgs, aKwargs != nullptr ? 0u : 1u, 1u, "set_int_max_str_digits"))
                return nullptr;
            if (!aArgs.empty())
                maxdigits = aArgs[0];
            if (maxdigits == nullptr)
                return error("TypeError", "set_int_max_str_digits() missing required argument 'maxdigits' (pos 1)");
            long long n = 0;
            if (!to_index(maxdigits, n))
                return nullptr;
            if (n != 0 && n < 640)
                return error("ValueError", "maxdigits must be >= 640 or 0 for unlimited");
            sIntMaxStrDigits = n;
            return (retain(none()), none());
        }
        object* sys_get_int_max_str_digits(std::vector<object*> const& aArgs, object*)
        {
            if (!arity(aArgs, 0u, 0u, "get_int_max_str_digits"))
                return nullptr;
            return make_int(sIntMaxStrDigits);
        }
        builtin_entry const sSysFunctions[] =
        {
            { "set_int_max_str_digits", sys_set_int_max_str_digits }, { "get_int_max_str_digits", sys_get_int_max_str_digits }
        };

        object* make_empty_module(std::string const& aName)
        {
            return &(new module_object{ { { &sModuleType.base } }, aName, {} })->h.base;
        }

        object* make_module(std::string const& aName)
        {
            auto* const m = new module_object{ { { &sModuleType.base } }, aName, {} };
            ref result{ &m->h.base };
            if (aName == "math")
            {
                for (auto const& f : math_module::sFunctions)
                    m->attributes.emplace_back(f.name, make_function(f.name, call_builtin, const_cast<builtin_entry*>(&f)));
                m->attributes.emplace_back("pi", make_float(3.141592653589793));
                m->attributes.emplace_back("e", make_float(2.718281828459045));
                m->attributes.emplace_back("tau", make_float(6.283185307179586));
                m->attributes.emplace_back("inf", make_float(std::numeric_limits<double>::infinity()));
                m->attributes.emplace_back("nan", make_float(std::numeric_limits<double>::quiet_NaN()));
            }
            else if (aName == "cmath")
            {
                for (auto const& f : cmath_module::sFunctions)
                    m->attributes.emplace_back(f.name, make_function(f.name, call_builtin, const_cast<builtin_entry*>(&f)));
                m->attributes.emplace_back("pi", make_float(3.141592653589793));
                m->attributes.emplace_back("e", make_float(2.718281828459045));
                m->attributes.emplace_back("tau", make_float(6.283185307179586));
                m->attributes.emplace_back("inf", make_float(std::numeric_limits<double>::infinity()));
                m->attributes.emplace_back("infj", make_complex(0.0, std::numeric_limits<double>::infinity()));
                m->attributes.emplace_back("nan", make_float(std::numeric_limits<double>::quiet_NaN()));
                m->attributes.emplace_back("nanj", make_complex(0.0, std::numeric_limits<double>::quiet_NaN()));
            }
            else if (aName == "sys")
            {
                m->attributes.emplace_back("maxsize", make_int(std::numeric_limits<long long>::max()));
                m->attributes.emplace_back("byteorder", make_str("little"));
                // (the program's command line arguments (the program first): none, [''])
                {
                    std::vector<object*> argv;
                    if (auto const* h = host(); h != nullptr && h->version >= 7u && h->arguments != nullptr)
                    {
                        std::vector<char const*> arguments(h->arguments(nullptr, 0u));
                        h->arguments(arguments.data(), static_cast<unsigned>(arguments.size()));
                        for (auto a : arguments)
                            argv.push_back(make_str(a));
                    }
                    if (argv.empty())
                        argv.push_back(make_str(""));
                    m->attributes.emplace_back("argv", make_list(std::move(argv)));
                }
#if defined(_WIN32)
                m->attributes.emplace_back("platform", make_str("win32"));
#elif defined(__APPLE__)
                m->attributes.emplace_back("platform", make_str("darwin"));
#else
                m->attributes.emplace_back("platform", make_str("linux"));
#endif
                m->attributes.emplace_back("version", make_str("3.13 (neos)"));
                m->attributes.emplace_back("float_repr_style", make_str("short"));
                for (auto const& f : sSysFunctions)
                    m->attributes.emplace_back(f.name, make_function(f.name, call_builtin, const_cast<builtin_entry*>(&f)));
                extend_sys(m->attributes);
            }
            else if (aName == "builtins")
            {
                // (the builtin functions, classes, exceptions and constants (not the compiler's runtime functions (e.g. __closure)))
                for (auto const& f : sBuiltinFunctions)
                    if (!std::string_view{ f.name }.starts_with("__"))
                        m->attributes.emplace_back(f.name, fn_builtin(f.name));
                for (auto const& c : sBuiltinClasses)
                    m->attributes.emplace_back(c.name, type_object_for(c.name, c.name));
                for (auto const& e : exception_bases())
                    if (e.first.find('.') == std::string::npos && e.first != "AxisError" && e.first != "UFuncTypeError" && e.first != "LinAlgError" &&
                        e.first != "ComplexWarning" && e.first != "DTypePromotionError")
                        m->attributes.emplace_back(e.first, exception_class(e.first));
                m->attributes.emplace_back("None", (retain(none()), none()));
                m->attributes.emplace_back("True", make_bool(true));
                m->attributes.emplace_back("False", make_bool(false));
                m->attributes.emplace_back("Ellipsis", (retain(&sEllipsis.base), &sEllipsis.base));
                m->attributes.emplace_back("NotImplemented", (retain(not_implemented()), not_implemented()));
                m->attributes.emplace_back("__debug__", make_bool(true));
                for (auto const name : { "__build_class__", "__import__" })
                    if (auto const f = fn_builtin(name))
                        m->attributes.emplace_back(name, f);
                    else
                        clear_error();
            }
            else if (auto const registered = modules().find(aName); registered != modules().end())
            {
                if (!registered->second(m->attributes))
                    return nullptr;
            }
            else
                return error("ModuleNotFoundError", "No module named '" + aName + "'");
            // (the functions of the modules that CPython implements in C (all of a module's, or those named) are not methods as a
            // class's attributes (e.g. class A: f = math.sqrt))
            static std::map<std::string, std::set<std::string>, std::less<>> const sCFunctions = { { "math", {} }, { "cmath", {} }, { "time", {} },
                { "_thread", {} }, { "gc", {} }, { "atexit", {} }, { "itertools", {} }, { "operator", {} }, { "_codecs", {} }, { "unicodedata", {} },
                { "binascii", {} }, { "struct", {} }, { "select", {} }, { "posix", {} }, { "nt", {} }, { "array", {} }, { "bisect", {} }, { "_string", {} },
                { "zlib", {} }, { "_csv", {} }, { "_suggestions", {} }, { "sys", {} }, { "_socket", {} }, { "_struct", {} }, { "_signal", {} }, { "marshal", {} }, { "_locale", {} },
                { "heapq", { "heappush", "heappop", "heapify", "heapreplace", "heappushpop" } }, { "functools", { "reduce", "cmp_to_key" } },
                { "warnings", { "warn", "warn_explicit", "_filters_mutated" } }, { "io", { "open", "open_code" } },
                { "keyword", { "iskeyword", "issoftkeyword" } } };
            if (auto const c = sCFunctions.find(aName); c != sCFunctions.end())
                for (auto& a : m->attributes)
                    if (a.second != nullptr && kind_of(a.second) == NEOS_PY_FUNCTION && as<function_object>(a.second)->self == nullptr &&
                        (c->second.empty() || c->second.contains(a.first)))
                        as<function_object>(a.second)->binds = false;
            return result.release_ownership();
        }

        // (the modules imported: sys.modules)
        std::map<std::string, object*>& imported()
        {
            // (__main__: the program's module)
            static std::map<std::string, object*> sModules = []()
            {
                auto* const main = new module_object{ { { &sModuleType.base } }, "__main__", {} };
                main->main = true;
                return std::map<std::string, object*>{ { "__main__", &main->h.base } };
            }();
            return sModules;
        }

        object* imported_modules()
        {
            static object* sDict = make_dict();
            for (auto const& m : imported())
                if (m.second != nullptr)
                {
                    ref const key{ make_str(m.first) };
                    dict_set(sDict, key.get(), m.second);
                }
            retain(sDict);
            return sDict;
        }

        bool modules_imported(char const* aName)
        {
            auto const m = imported().find(aName);
            return m != imported().end() && m->second != nullptr;
        }

        // python::import(name)
        object* fn_import(char const* aName)
        {
            auto& m = imported()[aName];
            if (m == nullptr)
            {
                m = make_module(aName);
                if (m == nullptr)
                {
                    imported().erase(aName);
                    return nullptr;
                }
                ref const modules{ imported_modules() };
                ref const key{ make_str(aName) };
                dict_set(modules.get(), key.get(), m);
            }
            retain(m);
            return m;
        }

        // python::import_module(name, prefix, init, getter, file): the module of a source file (its init: the function of its 
        // top-level statements, called when it is first imported)
        object* fn_import_module(char const* aName, std::int32_t aPrefix, object* aInit, object* aGetter, char const* aFile)
        {
            if (auto const existing = imported().find(aName); existing != imported().end() && existing->second != nullptr)
            {
                retain(existing->second);
                return existing->second;
            }
            source_modules()[aPrefix] = aName;
            auto* const m = new module_object{ { { &sModuleType.base } }, aName, {} };
            ref module{ &m->h.base };
            retain(aGetter);
            m->getter = aGetter;
            m->file = aFile;
            {
                ref const names{ call_module_getter(aGetter, -1) };
                if (!names)
                    return nullptr;
                for (auto n : tuple_items(names.get()))
                    m->names.push_back(as<str_object>(n)->value);
            }
            imported()[aName] = module.get();
            retain(module.get());
            {
                ref const modules{ imported_modules() };
                ref const key{ make_str(aName) };
                dict_set(modules.get(), key.get(), module.get());
            }
            // (a submodule: an attribute of its package)
            std::string const name{ aName };
            if (auto const dot = name.rfind('.'); dot != std::string::npos)
                if (auto const parent = imported().find(name.substr(0u, dot)); parent != imported().end() && parent->second != nullptr)
                    setattr(parent->second, name.substr(dot + 1u).c_str(), module.get());
            m->initializing = true;
            ref const args{ make_tuple() };
            ref const result{ call(aInit, args.get(), nullptr) };
            m->initializing = false;
            if (!result && failed())
            {
                // (it is not imported)
                auto const entry = imported().find(aName);
                if (entry != imported().end())
                {
                    release(entry->second);
                    imported().erase(entry);
                }
                ref const modules{ imported_modules() };
                ref const key{ make_str(aName) };
                if (auto const position = dict_find(modules.get(), key.get()); position >= 0)
                    dict_erase(*as<dict_object>(modules.get()), static_cast<std::size_t>(position));
                return nullptr;
            }
            return module.release_ownership();
        }

        // (from m import name: ImportError if m has no attribute name)
        object* fn_import_from(object* aModule, char const* aName)
        {
            auto const value = getattr(aModule, aName);
            if (value != nullptr || !std::string_view{ error_text() }.starts_with("AttributeError"))
                return value;
            clear_error();
            std::string name = "<unknown module name>";
            std::string where;
            bool partial = false;
            if (aModule != nullptr && aModule->type == &sModuleType.base)
            {
                auto const& m = *as<module_object>(aModule);
                name = m.name;
                partial = m.initializing;
                if (!m.file.empty())
                    where = " (" + m.file + ")";
                else
                    where = " (unknown location)";
            }
            if (partial)
                return error("ImportError", std::string{ "cannot import name '" } + aName + "' from partially initialized module '" + name + 
                    "' (most likely due to a circular import)" + where);
            return error("ImportError", std::string{ "cannot import name '" } + aName + "' from '" + name + "'" + where);
        }

        void fn_raise_import_error(char const* aMessage)
        {
            error("ImportError", aMessage);
        }

        // (a module that is not found: ModuleNotFoundError)
        object* fn_module_not_found(char const* aName)
        {
            return error("ModuleNotFoundError", std::string{ "No module named '" } + aName + "'");
        }

        object* fn_builtin(char const* aName)
        {
            std::string_view const name{ aName };
            for (auto const& entry : sBuiltinClasses)
                if (name == entry.name)
                    return type_object_for(entry.name, entry.name);
            for (auto const& entry : sBuiltinFunctions)
                if (name == entry.name)
                {
                    // (one function object for each builtin: "print is print")
                    static std::map<builtin_entry const*, object*> sFunctions;
                    auto& f = sFunctions[&entry];
                    if (f == nullptr)
                    {
                        f = make_function(entry.name, call_builtin, const_cast<builtin_entry*>(&entry));
                        as<function_object>(f)->binds = false;  // (a builtin is not a method (e.g. class A: f = len))
                    }
                    return new_reference(f);
                }
            if (name == "NotImplemented")
                return not_implemented();
            if (name == "Ellipsis")
                return &sEllipsis.base;
            if (is_exception_class(name))
                return exception_class(std::string{ name });
            return error("NameError", "name '" + std::string{ name } + "' is not defined");
        }

        // a, b = value
        // (calls with *iterable and **mapping arguments)
        void fn_args_extend(object* aList, object* aIterable)
        {
            auto const k = kind_of(aIterable);
            if (k == NEOS_PY_OTHER ? python_type(aIterable) == nullptr || (python_type(aIterable)->iter == nullptr && python_type(aIterable)->getitem == nullptr) :
                (k != NEOS_PY_LIST && k != NEOS_PY_TUPLE && k != NEOS_PY_STR && k != NEOS_PY_DICT && k != NEOS_PY_RANGE && k != NEOS_PY_ITERATOR))
            {
                error("TypeError", std::string{ "Value after * must be an iterable, not " } + type_name(aIterable));
                return;
            }
            std::vector<object*> items;
            if (!collect(aIterable, items))
            {
                for (auto i : items)
                    release(i);
                return;
            }
            for (auto i : items)
                as<list_object>(aList)->items.push_back(i);
        }
        // dir(object): the names of its attributes (sorted)
        void fn_args_extend(object* aList, object* aIterable);

        thread_local bool tObjectDir = false;

        object* b_dir(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "dir") || !arity(aArgs, 0u, 1u, "dir"))
                return nullptr;
            if (aArgs.empty())
                return error("SystemError", "dir() without arguments is compiled as the names of the scope");
            auto const target = aArgs[0];
            // (a __dir__ of the object's class (a user class's, or a class's metaclass's): its names, sorted (not for object.__dir__))
            if (!tObjectDir)
            {
                ref method;
                if (is_class(target))
                    method = ref{ meta_special(target, "__dir__") };
                else if (auto const cls = user_class_of(target); cls != nullptr && class_lookup(cls, "__dir__") != nullptr)
                    method = ref{ special_method(target, "__dir__") };
                if (method)
                {
                    ref const empty{ make_tuple() };
                    ref const names{ call(method.get(), empty.get(), nullptr) };
                    return names ? b_sorted({ names.get() }, nullptr) : nullptr;
                }
            }
            // (the names of the builtin types' instances, as CPython 3.13's dir() has them)
            static std::map<std::string, std::string, std::less<>> const sBuiltinNames =
            {
                { "str", "__add__ __class__ __contains__ __delattr__ __dir__ __doc__ __eq__ __format__ __ge__ __getattribute__ __getitem__ __getnewargs__ __getstate__ __gt__ __hash__ __init__ __init_subclass__ __iter__ __le__ __len__ __lt__ __mod__ __mul__ __ne__ __new__ __reduce__ __reduce_ex__ __repr__ __rmod__ __rmul__ __setattr__ __sizeof__ __str__ __subclasshook__ capitalize casefold center count encode endswith expandtabs find format format_map index isalnum isalpha isascii isdecimal isdigit isidentifier islower isnumeric isprintable isspace istitle isupper join ljust lower lstrip maketrans partition removeprefix removesuffix replace rfind rindex rjust rpartition rsplit rstrip split splitlines startswith strip swapcase title translate upper zfill" },
                { "list", "__add__ __class__ __class_getitem__ __contains__ __delattr__ __delitem__ __dir__ __doc__ __eq__ __format__ __ge__ __getattribute__ __getitem__ __getstate__ __gt__ __hash__ __iadd__ __imul__ __init__ __init_subclass__ __iter__ __le__ __len__ __lt__ __mul__ __ne__ __new__ __reduce__ __reduce_ex__ __repr__ __reversed__ __rmul__ __setattr__ __setitem__ __sizeof__ __str__ __subclasshook__ append clear copy count extend index insert pop remove reverse sort" },
                { "dict", "__class__ __class_getitem__ __contains__ __delattr__ __delitem__ __dir__ __doc__ __eq__ __format__ __ge__ __getattribute__ __getitem__ __getstate__ __gt__ __hash__ __init__ __init_subclass__ __ior__ __iter__ __le__ __len__ __lt__ __ne__ __new__ __or__ __reduce__ __reduce_ex__ __repr__ __reversed__ __ror__ __setattr__ __setitem__ __sizeof__ __str__ __subclasshook__ clear copy fromkeys get items keys pop popitem setdefault update values" },
                { "tuple", "__add__ __class__ __class_getitem__ __contains__ __delattr__ __dir__ __doc__ __eq__ __format__ __ge__ __getattribute__ __getitem__ __getnewargs__ __getstate__ __gt__ __hash__ __init__ __init_subclass__ __iter__ __le__ __len__ __lt__ __mul__ __ne__ __new__ __reduce__ __reduce_ex__ __repr__ __rmul__ __setattr__ __sizeof__ __str__ __subclasshook__ count index" },
                { "set", "__and__ __class__ __class_getitem__ __contains__ __delattr__ __dir__ __doc__ __eq__ __format__ __ge__ __getattribute__ __getstate__ __gt__ __hash__ __iand__ __init__ __init_subclass__ __ior__ __isub__ __iter__ __ixor__ __le__ __len__ __lt__ __ne__ __new__ __or__ __rand__ __reduce__ __reduce_ex__ __repr__ __ror__ __rsub__ __rxor__ __setattr__ __sizeof__ __str__ __sub__ __subclasshook__ __xor__ add clear copy difference difference_update discard intersection intersection_update isdisjoint issubset issuperset pop remove symmetric_difference symmetric_difference_update union update" },
                { "frozenset", "__and__ __class__ __class_getitem__ __contains__ __delattr__ __dir__ __doc__ __eq__ __format__ __ge__ __getattribute__ __getstate__ __gt__ __hash__ __init__ __init_subclass__ __iter__ __le__ __len__ __lt__ __ne__ __new__ __or__ __rand__ __reduce__ __reduce_ex__ __repr__ __ror__ __rsub__ __rxor__ __setattr__ __sizeof__ __str__ __sub__ __subclasshook__ __xor__ copy difference intersection isdisjoint issubset issuperset symmetric_difference union" },
                { "int", "__abs__ __add__ __and__ __bool__ __ceil__ __class__ __delattr__ __dir__ __divmod__ __doc__ __eq__ __float__ __floor__ __floordiv__ __format__ __ge__ __getattribute__ __getnewargs__ __getstate__ __gt__ __hash__ __index__ __init__ __init_subclass__ __int__ __invert__ __le__ __lshift__ __lt__ __mod__ __mul__ __ne__ __neg__ __new__ __or__ __pos__ __pow__ __radd__ __rand__ __rdivmod__ __reduce__ __reduce_ex__ __repr__ __rfloordiv__ __rlshift__ __rmod__ __rmul__ __ror__ __round__ __rpow__ __rrshift__ __rshift__ __rsub__ __rtruediv__ __rxor__ __setattr__ __sizeof__ __str__ __sub__ __subclasshook__ __truediv__ __trunc__ __xor__ as_integer_ratio bit_count bit_length conjugate denominator from_bytes imag is_integer numerator real to_bytes" },
                { "float", "__abs__ __add__ __bool__ __ceil__ __class__ __delattr__ __dir__ __divmod__ __doc__ __eq__ __float__ __floor__ __floordiv__ __format__ __ge__ __getattribute__ __getformat__ __getnewargs__ __getstate__ __gt__ __hash__ __init__ __init_subclass__ __int__ __le__ __lt__ __mod__ __mul__ __ne__ __neg__ __new__ __pos__ __pow__ __radd__ __rdivmod__ __reduce__ __reduce_ex__ __repr__ __rfloordiv__ __rmod__ __rmul__ __round__ __rpow__ __rsub__ __rtruediv__ __setattr__ __sizeof__ __str__ __sub__ __subclasshook__ __truediv__ __trunc__ as_integer_ratio conjugate fromhex hex imag is_integer real" },
                { "complex", "__abs__ __add__ __bool__ __class__ __complex__ __delattr__ __dir__ __doc__ __eq__ __format__ __ge__ __getattribute__ __getnewargs__ __getstate__ __gt__ __hash__ __init__ __init_subclass__ __le__ __lt__ __mul__ __ne__ __neg__ __new__ __pos__ __pow__ __radd__ __reduce__ __reduce_ex__ __repr__ __rmul__ __rpow__ __rsub__ __rtruediv__ __setattr__ __sizeof__ __str__ __sub__ __subclasshook__ __truediv__ conjugate imag real" },
                { "bool", "__abs__ __add__ __and__ __bool__ __ceil__ __class__ __delattr__ __dir__ __divmod__ __doc__ __eq__ __float__ __floor__ __floordiv__ __format__ __ge__ __getattribute__ __getnewargs__ __getstate__ __gt__ __hash__ __index__ __init__ __init_subclass__ __int__ __invert__ __le__ __lshift__ __lt__ __mod__ __mul__ __ne__ __neg__ __new__ __or__ __pos__ __pow__ __radd__ __rand__ __rdivmod__ __reduce__ __reduce_ex__ __repr__ __rfloordiv__ __rlshift__ __rmod__ __rmul__ __ror__ __round__ __rpow__ __rrshift__ __rshift__ __rsub__ __rtruediv__ __rxor__ __setattr__ __sizeof__ __str__ __sub__ __subclasshook__ __truediv__ __trunc__ __xor__ as_integer_ratio bit_count bit_length conjugate denominator from_bytes imag is_integer numerator real to_bytes" },
                { "bytes", "__add__ __buffer__ __bytes__ __class__ __contains__ __delattr__ __dir__ __doc__ __eq__ __format__ __ge__ __getattribute__ __getitem__ __getnewargs__ __getstate__ __gt__ __hash__ __init__ __init_subclass__ __iter__ __le__ __len__ __lt__ __mod__ __mul__ __ne__ __new__ __reduce__ __reduce_ex__ __repr__ __rmod__ __rmul__ __setattr__ __sizeof__ __str__ __subclasshook__ capitalize center count decode endswith expandtabs find fromhex hex index isalnum isalpha isascii isdigit islower isspace istitle isupper join ljust lower lstrip maketrans partition removeprefix removesuffix replace rfind rindex rjust rpartition rsplit rstrip split splitlines startswith strip swapcase title translate upper zfill" },
                { "bytearray", "__add__ __alloc__ __buffer__ __class__ __contains__ __delattr__ __delitem__ __dir__ __doc__ __eq__ __format__ __ge__ __getattribute__ __getitem__ __getstate__ __gt__ __hash__ __iadd__ __imul__ __init__ __init_subclass__ __iter__ __le__ __len__ __lt__ __mod__ __mul__ __ne__ __new__ __reduce__ __reduce_ex__ __release_buffer__ __repr__ __rmod__ __rmul__ __setattr__ __setitem__ __sizeof__ __str__ __subclasshook__ append capitalize center clear copy count decode endswith expandtabs extend find fromhex hex index insert isalnum isalpha isascii isdigit islower isspace istitle isupper join ljust lower lstrip maketrans partition pop remove removeprefix removesuffix replace reverse rfind rindex rjust rpartition rsplit rstrip split splitlines startswith strip swapcase title translate upper zfill" },
                { "range", "__bool__ __class__ __contains__ __delattr__ __dir__ __doc__ __eq__ __format__ __ge__ __getattribute__ __getitem__ __getstate__ __gt__ __hash__ __init__ __init_subclass__ __iter__ __le__ __len__ __lt__ __ne__ __new__ __reduce__ __reduce_ex__ __repr__ __reversed__ __setattr__ __sizeof__ __str__ __subclasshook__ count index start step stop" },
                { "NoneType", "__bool__ __class__ __delattr__ __dir__ __doc__ __eq__ __format__ __ge__ __getattribute__ __getstate__ __gt__ __hash__ __init__ __init_subclass__ __le__ __lt__ __ne__ __new__ __reduce__ __reduce_ex__ __repr__ __setattr__ __sizeof__ __str__ __subclasshook__" },
                { "function", "__annotations__ __builtins__ __call__ __class__ __closure__ __code__ __defaults__ __delattr__ __dict__ __dir__ __doc__ __eq__ __format__ __ge__ __get__ __getattribute__ __getstate__ __globals__ __gt__ __hash__ __init__ __init_subclass__ __kwdefaults__ __le__ __lt__ __module__ __name__ __ne__ __new__ __qualname__ __reduce__ __reduce_ex__ __repr__ __setattr__ __sizeof__ __str__ __subclasshook__ __type_params__" },
                { "property", "__class__ __delattr__ __delete__ __dir__ __doc__ __eq__ __format__ __ge__ __get__ __getattribute__ __getstate__ __gt__ __hash__ __init__ __init_subclass__ __isabstractmethod__ __le__ __lt__ __name__ __ne__ __new__ __reduce__ __reduce_ex__ __repr__ __set__ __set_name__ __setattr__ __sizeof__ __str__ __subclasshook__ deleter fdel fget fset getter setter" },
                { "staticmethod", "__call__ __class__ __delattr__ __dict__ __dir__ __doc__ __eq__ __format__ __func__ __ge__ __get__ __getattribute__ __getstate__ __gt__ __hash__ __init__ __init_subclass__ __isabstractmethod__ __le__ __lt__ __ne__ __new__ __reduce__ __reduce_ex__ __repr__ __setattr__ __sizeof__ __str__ __subclasshook__ __wrapped__" },
                { "classmethod", "__class__ __delattr__ __dict__ __dir__ __doc__ __eq__ __format__ __func__ __ge__ __get__ __getattribute__ __getstate__ __gt__ __hash__ __init__ __init_subclass__ __isabstractmethod__ __le__ __lt__ __ne__ __new__ __reduce__ __reduce_ex__ __repr__ __setattr__ __sizeof__ __str__ __subclasshook__ __wrapped__" },
                { "object", "__class__ __delattr__ __dir__ __doc__ __eq__ __format__ __ge__ __getattribute__ __getstate__ __gt__ __hash__ __init__ __init_subclass__ __le__ __lt__ __ne__ __new__ __reduce__ __reduce_ex__ __repr__ __setattr__ __sizeof__ __str__ __subclasshook__" }
            };
            std::set<std::string> names;
            auto const add_words = [&](std::string_view aWords)
            {
                std::size_t start = 0u;
                while (start < aWords.size())
                {
                    auto end = aWords.find(' ', start);
                    if (end == std::string_view::npos)
                        end = aWords.size();
                    names.insert(std::string{ aWords.substr(start, end - start) });
                    start = end + 1u;
                }
            };
            auto const add_class = [&](object* aClass)
            {
                for (auto const& a : as<class_object>(aClass)->attributes)
                    names.insert(a.first);
                for (auto c : as<class_object>(aClass)->mro)
                    for (auto const& a : as<class_object>(c)->attributes)
                        names.insert(a.first);
                auto const& base = as<class_object>(aClass)->builtinBase;
                if (auto const builtin = sBuiltinNames.find(base.empty() ? std::string{ "object" } : base); builtin != sBuiltinNames.end())
                    add_words(builtin->second);
                if (!as<class_object>(aClass)->slots)
                    names.insert({ "__dict__", "__weakref__" });
                names.insert({ "__module__", "__doc__" });
            };
            if (auto const cls = user_class_of(target))
            {
                if (auto const method = class_lookup(cls, "__dir__"))
                {
                    ref const bound{ bind_attribute(method, target, cls) };
                    ref const noArgs{ make_tuple() };
                    ref const result{ call(bound.get(), noArgs.get(), nullptr) };
                    if (!result)
                        return nullptr;
                    ref const items{ make_list() };
                    fn_args_extend(items.get(), result.get());
                    if (failed())
                        return nullptr;
                    ref const sortArgs{ make_tuple({ (retain(items.get()), items.get()) }) };
                    return b_sorted(tuple_items(sortArgs.get()), nullptr);
                }
                add_class(cls);
                if (is_instance(target))
                    if (auto const d = as<instance_object>(target)->attributes.dict)
                        for (auto const& item : as<dict_object>(d)->items)
                            if (kind_of(item.first) == NEOS_PY_STR)
                                names.insert(as<str_object>(item.first)->value);
            }
            else if (is_class(target))
                add_class(target);
            else if (target->type == &sModuleType.base)
            {
                for (auto const& a : as<module_object>(target)->attributes)
                    names.insert(a.first);
                names.insert({ "__doc__", "__loader__", "__name__", "__package__", "__spec__" });
            }
            else
            {
                std::string kind = is_type_object(target) ? as<type_object>(target)->qualifiedName : type_name(target);
                if (target->type == &sScriptFunctionType.base)
                    kind = "function";
                if (auto const builtin = sBuiltinNames.find(kind); builtin != sBuiltinNames.end())
                    add_words(builtin->second);
                else
                    add_words(sBuiltinNames.at("object"));
            }
            std::vector<neos_py_object*> result;
            for (auto const& n : names)
                result.push_back(make_str(n));
            return make_list(std::move(result));
        }

        void fn_kwargs_update(object* aDict, object* aMapping)
        {
            aMapping = builtin_value(aMapping);
            if (kind_of(aMapping) != NEOS_PY_DICT)
            {
                error("TypeError", std::string{ "argument after ** must be a mapping, not " } + type_name(aMapping));
                return;
            }
            for (auto const& item : as<dict_object>(aMapping)->items)
            {
                if (kind_of(item.first) != NEOS_PY_STR)
                {
                    error("TypeError", "keywords must be strings");
                    return;
                }
                for (auto const& existing : as<dict_object>(aDict)->items)
                    if (as<str_object>(existing.first)->value == as<str_object>(item.first)->value)
                    {
                        error("TypeError", "got multiple values for keyword argument '" + as<str_object>(item.first)->value + "'");
                        return;
                    }
                dict_set(aDict, item.first, item.second);
            }
        }
        object* fn_list_tuple(object* aList)
        {
            auto items = as<list_object>(aList)->items;
            for (auto i : items)
                retain(i);
            return make_tuple(std::move(items));
        }

        object* fn_unpack(object* aValue, std::int64_t aCount)
        {
            auto const k = kind_of(aValue);
            if (k == NEOS_PY_OTHER ? python_type(aValue) == nullptr || (python_type(aValue)->iter == nullptr && python_type(aValue)->getitem == nullptr) :
                (k != NEOS_PY_LIST && k != NEOS_PY_TUPLE && k != NEOS_PY_STR && k != NEOS_PY_DICT && k != NEOS_PY_RANGE && k != NEOS_PY_ITERATOR))
                return error("TypeError", std::string{ "cannot unpack non-iterable " } + type_name(aValue) + " object");
            std::vector<object*> items;
            if (!collect(aValue, items))
            {
                for (auto i : items)
                    release(i);
                return nullptr;
            }
            if (static_cast<std::int64_t>(items.size()) != aCount)
            {
                auto const n = items.size();
                for (auto i : items)
                    release(i);
                if (static_cast<std::int64_t>(n) > aCount)
                    return error("ValueError", "too many values to unpack (expected " + std::to_string(aCount) + ")");
                return error("ValueError", "not enough values to unpack (expected " + std::to_string(aCount) + ", got " + std::to_string(n) + ")");
            }
            return make_tuple(std::move(items));
        }

        // (a, *b, c = value: the values as a tuple, the starred target's as a list)
        object* fn_unpack_ex(object* aValue, std::int64_t aCount, std::int64_t aStarred)
        {
            auto const k = kind_of(aValue);
            if (k == NEOS_PY_OTHER ? python_type(aValue) == nullptr || (python_type(aValue)->iter == nullptr && python_type(aValue)->getitem == nullptr) :
                (k != NEOS_PY_LIST && k != NEOS_PY_TUPLE && k != NEOS_PY_STR && k != NEOS_PY_DICT && k != NEOS_PY_RANGE && k != NEOS_PY_ITERATOR))
                return error("TypeError", std::string{ "cannot unpack non-iterable " } + type_name(aValue) + " object");
            std::vector<object*> items;
            if (!collect(aValue, items))
            {
                for (auto i : items)
                    release(i);
                return nullptr;
            }
            auto const fixed = static_cast<std::size_t>(aCount - 1);
            if (items.size() < fixed)
            {
                auto const n = items.size();
                for (auto i : items)
                    release(i);
                return error("ValueError", "not enough values to unpack (expected at least " + std::to_string(fixed) + ", got " + std::to_string(n) + ")");
            }
            auto const before = static_cast<std::size_t>(aStarred);
            auto const after = fixed - before;
            std::vector<object*> result{ items.begin(), items.begin() + static_cast<std::ptrdiff_t>(before) };
            result.push_back(make_list(std::vector<object*>{ items.begin() + static_cast<std::ptrdiff_t>(before), items.end() - static_cast<std::ptrdiff_t>(after) }));
            result.insert(result.end(), items.end() - static_cast<std::ptrdiff_t>(after), items.end());
            return make_tuple(std::move(result));
        }

        object* fn_tuple_get(object* aTuple, std::int64_t aIndex)
        {
            auto const item = as<tuple_object>(aTuple)->items[static_cast<std::size_t>(aIndex)];
            retain(item);
            return item;
        }

        // (builtins.py's stubs whose argument is an object)
        void fn_print_object(object* aObject)
        {
            std::string text;
            if (str_of(aObject, text))
                write_out(text + "\n");
        }
        object* fn_abs_object(object* aObject) { return checked(unary(NEOS_PY_ABSOLUTE, aObject)); }
        bool fn_bool_object(object* aObject) { return truth(aObject) == 1; }

        constexpr auto OBJ = NEOS_RT_OBJECT;
        constexpr auto I64 = NEOS_RT_I64;
        constexpr auto I32 = NEOS_RT_I32;
        constexpr auto F64 = NEOS_RT_F64;
        constexpr auto STR = NEOS_RT_STRING;
        constexpr auto BOOL = NEOS_RT_BOOL;
        constexpr auto VOID = NEOS_RT_VOID;
        constexpr auto INT = NEOS_RT_INTEGER;
    }

    using namespace detail;

    std::vector<neos_rt_function> const& object_functions()
    {
        static std::vector<neos_rt_function> const sFunctions =
        {
            { "context", NEOS_RT_FUNCTION(fn_context), OBJ, 0u, {}, 0u },
            { "none", NEOS_RT_FUNCTION(fn_none), OBJ, 0u, {}, 0u },
            { "ellipsis", NEOS_RT_FUNCTION(fn_ellipsis), OBJ, 0u, {}, 0u },
            { "from_bool", NEOS_RT_FUNCTION(fn_from_bool), OBJ, 1u, { BOOL }, 0u },
            { "from_int", NEOS_RT_FUNCTION(fn_from_int), OBJ, 1u, { I64 }, 0u },
            { "from_float", NEOS_RT_FUNCTION(fn_from_float), OBJ, 1u, { F64 }, 0u },
            { "from_str", NEOS_RT_FUNCTION(fn_from_str), OBJ, 1u, { STR }, 0u },
            { "from_complex", NEOS_RT_FUNCTION(fn_from_complex), OBJ, 2u, { F64, F64 }, 0u },
            { "truth", NEOS_RT_FUNCTION(fn_truth), BOOL, 1u, { OBJ }, 0u },
            { "str", NEOS_RT_FUNCTION(fn_str), STR, 1u, { OBJ }, 0u },
            { "repr", NEOS_RT_FUNCTION(fn_repr), STR, 1u, { OBJ }, 0u },
            { "format_field", NEOS_RT_FUNCTION(fn_format_field), STR, 3u, { OBJ, I32, STR }, 0u },
            { "__fstring(object,integer,string)", NEOS_RT_FUNCTION(fn_fstring_integer), STR, 3u, { OBJ, INT, STR }, 0u },
            { "to_int", NEOS_RT_FUNCTION(fn_to_int), I64, 1u, { OBJ }, 0u },
            { "integer_binary", NEOS_RT_FUNCTION(fn_integer_binary), INT, 3u, { I32, INT, INT }, 0u },
            { "integer_unary", NEOS_RT_FUNCTION(fn_integer_unary), INT, 2u, { I32, INT }, 0u },
            { "integer_compare", NEOS_RT_FUNCTION(fn_integer_compare), BOOL, 3u, { I32, INT, INT }, 0u },
            { "integer_true_divide", NEOS_RT_FUNCTION(fn_integer_true_divide), F64, 2u, { INT, INT }, 0u },
            { "integer_float", NEOS_RT_FUNCTION(fn_integer_float), F64, 1u, { INT }, 0u },
            { "integer_i64", NEOS_RT_FUNCTION(fn_integer_i64), I64, 1u, { INT }, 0u },
            { "integer_str", NEOS_RT_FUNCTION(fn_integer_str), STR, 1u, { INT }, 0u },
            { "integer_from_i64", NEOS_RT_FUNCTION(fn_integer_from_i64), INT, 1u, { I64 }, 0u },
            { "integer_from_float", NEOS_RT_FUNCTION(fn_integer_from_float), INT, 1u, { F64 }, 0u },
            { "integer_from_string", NEOS_RT_FUNCTION(fn_integer_from_string), INT, 1u, { STR }, 0u },
            { "to_integer", NEOS_RT_FUNCTION(fn_to_integer), INT, 1u, { OBJ }, 0u },
            { "from_integer", NEOS_RT_FUNCTION(fn_from_integer), OBJ, 1u, { INT }, 0u },
            { "print(integer)", NEOS_RT_FUNCTION(fn_print_integer), VOID, 1u, { INT }, 0u },
            { "str(integer)", NEOS_RT_FUNCTION(fn_integer_str), STR, 1u, { INT }, 0u },
            { "int(string)", NEOS_RT_FUNCTION(fn_int_string), INT, 1u, { STR }, 0u },
            { "abs(integer)", NEOS_RT_FUNCTION(fn_abs_integer), INT, 1u, { INT }, 0u },
            { "to_float", NEOS_RT_FUNCTION(fn_to_float), F64, 1u, { OBJ }, 0u },
            { "binary", NEOS_RT_FUNCTION(fn_binary), OBJ, 3u, { I32, OBJ, OBJ }, 0u },
            { "int_from_string", NEOS_RT_FUNCTION(fn_int_from_string), OBJ, 1u, { STR }, 0u },
            { "inplace", NEOS_RT_FUNCTION(fn_inplace), OBJ, 3u, { I32, OBJ, OBJ }, 0u },
            { "unary", NEOS_RT_FUNCTION(fn_unary), OBJ, 2u, { I32, OBJ }, 0u },
            { "compare", NEOS_RT_FUNCTION(fn_compare), OBJ, 3u, { I32, OBJ, OBJ }, 0u },
            { "compare_truth", NEOS_RT_FUNCTION(fn_compare_truth), BOOL, 3u, { I32, OBJ, OBJ }, 0u },
            { "binary_io", NEOS_RT_FUNCTION(fn_binary_io), OBJ, 3u, { I32, INT, OBJ }, 0u },
            { "binary_oi", NEOS_RT_FUNCTION(fn_binary_oi), OBJ, 3u, { I32, OBJ, INT }, 0u },
            { "compare_io", NEOS_RT_FUNCTION(fn_compare_io), OBJ, 3u, { I32, INT, OBJ }, 0u },
            { "compare_oi", NEOS_RT_FUNCTION(fn_compare_oi), OBJ, 3u, { I32, OBJ, INT }, 0u },
            { "compare_io_truth", NEOS_RT_FUNCTION(fn_compare_io_truth), BOOL, 3u, { I32, INT, OBJ }, 0u },
            { "compare_oi_truth", NEOS_RT_FUNCTION(fn_compare_oi_truth), BOOL, 3u, { I32, OBJ, INT }, 0u },
            { "compare_text", NEOS_RT_FUNCTION(fn_compare_text), OBJ, 3u, { I32, OBJ, STR }, 0u },
            { "compare_text_truth", NEOS_RT_FUNCTION(fn_compare_text_truth), BOOL, 3u, { I32, OBJ, STR }, 0u },
            { "is", NEOS_RT_FUNCTION(fn_is), BOOL, 2u, { OBJ, OBJ }, 0u },
            { "contains", NEOS_RT_FUNCTION(fn_contains), BOOL, 2u, { OBJ, OBJ }, 0u },
            { "getattr", NEOS_RT_FUNCTION(fn_getattr), OBJ, 2u, { OBJ, STR }, 0u },
            { "setattr", NEOS_RT_FUNCTION(fn_setattr), VOID, 3u, { OBJ, STR, OBJ }, 0u },
            { "call", NEOS_RT_FUNCTION(fn_call), OBJ, 3u, { OBJ, OBJ, OBJ }, 0u },
            { "load_method", NEOS_RT_FUNCTION(fn_load_method), OBJ, 2u, { OBJ, STR }, 0u },
            { "call_arguments", NEOS_RT_FUNCTION(fn_call_arguments), OBJ, 4u, { OBJ, OBJ, OBJ, OBJ }, 0u },
            { "direct_function", NEOS_RT_FUNCTION(fn_direct_function), I32, 2u, { OBJ, I64 }, 0u },
            { "call_method", NEOS_RT_FUNCTION(fn_call_method), INT, 4u, { OBJ, STR, OBJ, OBJ }, 0u },
            { "function", NEOS_RT_FUNCTION(fn_function), OBJ, 4u, { STR, I32, STR, OBJ }, 0u },
            { "getitem", NEOS_RT_FUNCTION(fn_getitem), OBJ, 2u, { OBJ, OBJ }, 0u },
            { "getitem_integer", NEOS_RT_FUNCTION(fn_getitem_integer), OBJ, 2u, { OBJ, INT }, 0u },
            { "setitem", NEOS_RT_FUNCTION(fn_setitem), VOID, 3u, { OBJ, OBJ, OBJ }, 0u },
            { "delitem", NEOS_RT_FUNCTION(fn_delitem), VOID, 2u, { OBJ, OBJ }, 0u },
            { "delattr", NEOS_RT_FUNCTION(fn_delattr), VOID, 2u, { OBJ, STR }, 0u },
            { "len", NEOS_RT_FUNCTION(fn_len), I64, 1u, { OBJ }, 0u },
            { "__yield(object)", NEOS_RT_FUNCTION(fn_yield), OBJ, 1u, { OBJ }, 0u },
            { "__any_items(object)", NEOS_RT_FUNCTION(fn_any_items), OBJ, 1u, { OBJ }, 0u },
            { "__all_items(object)", NEOS_RT_FUNCTION(fn_all_items), OBJ, 1u, { OBJ }, 0u },
            { "iter", NEOS_RT_FUNCTION(fn_iter), OBJ, 1u, { OBJ }, 0u },
            { "next", NEOS_RT_FUNCTION(fn_next), OBJ, 1u, { OBJ }, 0u },
            { "range_length", NEOS_RT_FUNCTION(fn_range_length), NEOS_RT_I64, 1u, { OBJ }, 0u },
            { "sequence_length", NEOS_RT_FUNCTION(fn_sequence_length), NEOS_RT_I64, 1u, { OBJ }, 0u },
            { "sequence_item", NEOS_RT_FUNCTION(fn_sequence_item), OBJ, 2u, { OBJ, I64 }, 0u },
            { "sequence_int", NEOS_RT_FUNCTION(fn_sequence_int), NEOS_RT_I64, 2u, { OBJ, I64 }, 0u },
            { "range_start", NEOS_RT_FUNCTION(fn_range_start), NEOS_RT_I64, 1u, { OBJ }, 0u },
            { "range_step", NEOS_RT_FUNCTION(fn_range_step), NEOS_RT_I64, 1u, { OBJ }, 0u },
            { "tuple", NEOS_RT_FUNCTION(fn_tuple), OBJ, 1u, { I64 }, 0u },
            { "tuple_set", NEOS_RT_FUNCTION(fn_tuple_set), VOID, 3u, { OBJ, I64, OBJ }, 0u },
            { "list", NEOS_RT_FUNCTION(fn_list), OBJ, 0u, {}, 0u },
            { "list_append", NEOS_RT_FUNCTION(fn_list_append), VOID, 2u, { OBJ, OBJ }, 0u },
            { "dict", NEOS_RT_FUNCTION(fn_dict), OBJ, 0u, {}, 0u },
            { "dict_set", NEOS_RT_FUNCTION(fn_dict_set), VOID, 3u, { OBJ, OBJ, OBJ }, 0u },
            { "set", NEOS_RT_FUNCTION(fn_set), OBJ, 0u, {}, 0u },
            { "set_add", NEOS_RT_FUNCTION(fn_set_add), VOID, 2u, { OBJ, OBJ }, 0u },
            { "set_update", NEOS_RT_FUNCTION(fn_set_update), VOID, 2u, { OBJ, OBJ }, 0u },
            { "ns_set", NEOS_RT_FUNCTION(fn_ns_set), VOID, 3u, { OBJ, STR, OBJ }, 0u },
            { "ns_cell", NEOS_RT_FUNCTION(fn_ns_cell), VOID, 3u, { OBJ, STR, OBJ }, 0u },
            { "ns_classcell", NEOS_RT_FUNCTION(fn_ns_classcell), VOID, 2u, { OBJ, OBJ }, 0u },
            { "unbound", NEOS_RT_FUNCTION(fn_unbound), VOID, 2u, { STR, I32 }, 0u },
            { "dict_merge", NEOS_RT_FUNCTION(fn_dict_merge), VOID, 2u, { OBJ, OBJ }, 0u },
            { "set_of_constants", NEOS_RT_FUNCTION(fn_set_of_constants), VOID, 1u, { OBJ }, 0u },
            { "kwarg", NEOS_RT_FUNCTION(fn_kwarg), VOID, 3u, { OBJ, STR, OBJ }, 0u },
            { "slice", NEOS_RT_FUNCTION(fn_slice), OBJ, 3u, { OBJ, OBJ, OBJ }, 0u },
            { "range", NEOS_RT_FUNCTION(fn_range), OBJ, 3u, { I64, I64, I64 }, 0u },
            { "list_of", NEOS_RT_FUNCTION(fn_list_of), OBJ, 1u, { OBJ }, 0u },
            { "tuple_of", NEOS_RT_FUNCTION(fn_tuple_of), OBJ, 1u, { OBJ }, 0u },
            { "builtin", NEOS_RT_FUNCTION(fn_builtin), OBJ, 1u, { STR }, 0u },
            { "unpack", NEOS_RT_FUNCTION(fn_unpack), OBJ, 2u, { OBJ, I64 }, 0u },
            { "unpack_ex", NEOS_RT_FUNCTION(fn_unpack_ex), OBJ, 3u, { OBJ, I64, I64 }, 0u },
            { "args_extend", NEOS_RT_FUNCTION(fn_args_extend), VOID, 2u, { OBJ, OBJ }, 0u },
            { "kwargs_update", NEOS_RT_FUNCTION(fn_kwargs_update), VOID, 2u, { OBJ, OBJ }, 0u },
            { "list_tuple", NEOS_RT_FUNCTION(fn_list_tuple), OBJ, 1u, { OBJ }, 0u },
            { "raise", NEOS_RT_FUNCTION(fn_raise_statement), VOID, 1u, { OBJ }, 0u },
            { "raise_from", NEOS_RT_FUNCTION(fn_raise_from_statement), VOID, 2u, { OBJ, OBJ }, 0u },
            { "propagate", NEOS_RT_FUNCTION(fn_raise), VOID, 1u, { OBJ }, 0u },
            { "unhandled", NEOS_RT_FUNCTION(fn_unhandled), I32, 1u, { STR }, 0u },
            { "finish", NEOS_RT_FUNCTION(fn_finish), VOID, 0u, {}, 0u },
            { "depth_check", NEOS_RT_FUNCTION(fn_depth_check), VOID, 1u, { I32 }, 0u },
            { "zero_division", NEOS_RT_FUNCTION(fn_zero_division), VOID, 1u, { I32 }, 0u },
            { "import", NEOS_RT_FUNCTION(fn_import), OBJ, 1u, { STR }, 0u },
            { "import_module", NEOS_RT_FUNCTION(fn_import_module), OBJ, 5u, { STR, I32, OBJ, OBJ, STR }, 0u },
            { "module_not_found", NEOS_RT_FUNCTION(fn_module_not_found), OBJ, 1u, { STR }, 0u },
            { "import_from", NEOS_RT_FUNCTION(fn_import_from), OBJ, 2u, { OBJ, STR }, 0u },
            { "module_namespace", NEOS_RT_FUNCTION(fn_module_namespace), OBJ, 2u, { STR, OBJ }, 0u },
            { "global_lookup", NEOS_RT_FUNCTION(fn_global_lookup), OBJ, 2u, { STR, STR }, 0u },
            { "import_star", NEOS_RT_FUNCTION(fn_import_star), VOID, 2u, { STR, OBJ }, 0u },
            { "register_namespace", NEOS_RT_FUNCTION(fn_register_namespace), VOID, 2u, { STR, OBJ }, 0u },
            { "exec_lookup", NEOS_RT_FUNCTION(fn_exec_lookup), OBJ, 1u, { STR }, 0u },
            { "exec_globals", NEOS_RT_FUNCTION(fn_exec_globals), OBJ, 0u, {}, 0u },
            { "exec_read", NEOS_RT_FUNCTION(fn_exec_read), OBJ, 2u, { STR, OBJ }, 0u },
            { "exec_store", NEOS_RT_FUNCTION(fn_exec_store), VOID, 2u, { STR, OBJ }, 0u },
            { "sorted_keys", NEOS_RT_FUNCTION(fn_sorted_keys), OBJ, 1u, { OBJ }, 0u },
            { "raise_import_error", NEOS_RT_FUNCTION(fn_raise_import_error), VOID, 1u, { STR }, 0u },
            { "reraise", NEOS_RT_FUNCTION(fn_reraise), VOID, 0u, {}, 0u },
            { "handled", NEOS_RT_FUNCTION(fn_handled), VOID, 0u, {}, 0u },
            { "with_enter", NEOS_RT_FUNCTION(fn_with_enter), OBJ, 1u, { OBJ }, 0u },
            { "with_exit", NEOS_RT_FUNCTION(fn_with_exit), VOID, 1u, { OBJ }, 0u },
            { "with_exit_exception", NEOS_RT_FUNCTION(fn_with_exit_exception), BOOL, 2u, { OBJ, STR }, 0u },
            { "caught", NEOS_RT_FUNCTION(fn_caught), OBJ, 1u, { STR }, 0u },
            { "exception_matches", NEOS_RT_FUNCTION(fn_exception_matches), BOOL, 2u, { OBJ, OBJ }, 0u },
            { "assertion", NEOS_RT_FUNCTION(fn_assertion), VOID, 1u, { OBJ }, 0u },
            { "tuple_get", NEOS_RT_FUNCTION(fn_tuple_get), OBJ, 2u, { OBJ, I64 }, 0u },
            { "print(object)", NEOS_RT_FUNCTION(fn_print_object), VOID, 1u, { OBJ }, 0u },
            { "str(object)", NEOS_RT_FUNCTION(fn_str), STR, 1u, { OBJ }, 0u },
            { "repr(object)", NEOS_RT_FUNCTION(fn_repr), STR, 1u, { OBJ }, 0u },
            { "len(object)", NEOS_RT_FUNCTION(fn_len_integer), INT, 1u, { OBJ }, 0u },
            { "int(object)", NEOS_RT_FUNCTION(fn_to_integer), INT, 1u, { OBJ }, 0u },
            { "float(object)", NEOS_RT_FUNCTION(fn_to_float), F64, 1u, { OBJ }, 0u },
            { "abs(object)", NEOS_RT_FUNCTION(fn_abs_object), OBJ, 1u, { OBJ }, 0u },
            { "bool(object)", NEOS_RT_FUNCTION(fn_bool_object), BOOL, 1u, { OBJ }, 0u },
            { "__is_int(object)", NEOS_RT_FUNCTION(fn_is_int), BOOL, 1u, { OBJ }, 0u },
            { "__as_int(object)", NEOS_RT_FUNCTION(fn_to_integer), INT, 1u, { OBJ }, 0u }
        };
        return sFunctions;
    }
}
