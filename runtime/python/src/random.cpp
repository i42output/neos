/*
  random.cpp

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

  Portions of this file are derived from CPython 3.13 (Modules/_randommodule.c and Lib/random.py), Copyright (c) 2001-2024
  Python Software Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2
  (see runtime/python/LICENSE.PSF). Changes: translated from C and Python to C++ for the neos Python runtime plugin and
  adapted to its object model (see runtime/python/NOTICE).

  The Mersenne Twister (Modules/_randommodule.c's, from the original code: its verbatim comments):

  A C-program for MT19937, with initialization improved 2002/1/26.
  Coded by Takuji Nishimura and Makoto Matsumoto.

  Before using, initialize the state by using init_genrand(seed)
  or init_by_array(init_key, key_length).

  Copyright (C) 1997 - 2002, Makoto Matsumoto and Takuji Nishimura,
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.

   3. The names of its contributors may not be used to endorse or promote
      products derived from this software without specific prior written
      permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT
  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
  TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

  Any feedback is very welcome.
  http://www.math.keio.ac.jp/matumoto/emt.html
*/

// The module random: CPython 3.13's Mersenne Twister (its seeding) and random.py's algorithms (the same sequences for the same seeds)

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <numbers>
#include <random>
#include <string>
#include <vector>
#include "objects.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        namespace
        {
            object* borrowed(object* aObject) { retain(aObject); return aObject; }

            object* call_with(object* aCallable, std::vector<object*> aArgs, object* aKwargs = nullptr)    // (steals the arguments)
            {
                ref const args{ make_tuple(std::move(aArgs)) };
                return call(aCallable, args.get(), aKwargs);
            }

            constexpr int N = 624;
            constexpr int M = 397;

            struct mersenne_twister
            {
                std::array<std::uint32_t, N> mt{};
                int index = N + 1;

                void init_genrand(std::uint32_t s)
                {
                    mt[0] = s;
                    for (int i = 1; i < N; ++i)
                        mt[i] = 1812433253u * (mt[i - 1] ^ (mt[i - 1] >> 30)) + static_cast<std::uint32_t>(i);
                    index = N;
                }
                void init_by_array(std::vector<std::uint32_t> const& aKey)
                {
                    init_genrand(19650218u);
                    std::size_t i = 1u, j = 0u;
                    auto const length = aKey.size();
                    for (auto k = std::max<std::size_t>(N, length); k > 0u; --k)
                    {
                        mt[i] = (mt[i] ^ ((mt[i - 1u] ^ (mt[i - 1u] >> 30)) * 1664525u)) + aKey[j] + static_cast<std::uint32_t>(j);
                        ++i;
                        ++j;
                        if (i >= N)
                        {
                            mt[0] = mt[N - 1];
                            i = 1u;
                        }
                        if (j >= length)
                            j = 0u;
                    }
                    for (auto k = N - 1; k > 0; --k)
                    {
                        mt[i] = (mt[i] ^ ((mt[i - 1u] ^ (mt[i - 1u] >> 30)) * 1566083941u)) - static_cast<std::uint32_t>(i);
                        ++i;
                        if (i >= N)
                        {
                            mt[0] = mt[N - 1];
                            i = 1u;
                        }
                    }
                    mt[0] = 0x80000000u;
                }
                std::uint32_t next()
                {
                    static std::uint32_t const mag01[2] = { 0u, 0x9908b0dfu };
                    if (index >= N)
                    {
                        int kk = 0;
                        for (; kk < N - M; ++kk)
                        {
                            auto const y = (mt[kk] & 0x80000000u) | (mt[kk + 1] & 0x7fffffffu);
                            mt[kk] = mt[kk + M] ^ (y >> 1) ^ mag01[y & 1u];
                        }
                        for (; kk < N - 1; ++kk)
                        {
                            auto const y = (mt[kk] & 0x80000000u) | (mt[kk + 1] & 0x7fffffffu);
                            mt[kk] = mt[kk + (M - N)] ^ (y >> 1) ^ mag01[y & 1u];
                        }
                        auto const y = (mt[N - 1] & 0x80000000u) | (mt[0] & 0x7fffffffu);
                        mt[N - 1] = mt[M - 1] ^ (y >> 1) ^ mag01[y & 1u];
                        index = 0;
                    }
                    auto y = mt[index++];
                    y ^= (y >> 11);
                    y ^= (y << 7) & 0x9d2c5680u;
                    y ^= (y << 15) & 0xefc60000u;
                    y ^= (y >> 18);
                    return y;
                }
                double random()
                {
                    auto const a = next() >> 5;
                    auto const b = next() >> 6;
                    return (a * 67108864.0 + b) * (1.0 / 9007199254740992.0);
                }
            };

            struct random_object
            {
                header h;
                mersenne_twister state;
                object* gaussNext = nullptr;
                ~random_object() { release(gaussNext); }
            };

            neos_py_type make_random_type();
            neos_py_type sRandomType = make_random_type();
            random_object& as_random(object* aObject) { return *reinterpret_cast<random_object*>(aObject); }

            // (an int's absolute value as 32-bit words, least significant first)
            bool words_of(object* aInt, std::vector<std::uint32_t>& aWords)
            {
                long long small = 0;
                if (to_index(aInt, small) && small != std::numeric_limits<long long>::min())
                {
                    auto u = static_cast<std::uint64_t>(small < 0 ? -small : small);
                    aWords.push_back(static_cast<std::uint32_t>(u));
                    if ((u >> 32) != 0u)
                        aWords.push_back(static_cast<std::uint32_t>(u >> 32));
                    return true;
                }
                clear_error();
                ref value{ unary(NEOS_PY_ABSOLUTE, aInt) };
                ref const mask{ make_int(0xFFFFFFFFll) };
                ref const shift{ make_int(32) };
                ref const zero{ make_int(0) };
                for (;;)
                {
                    ref const low{ binary(NEOS_PY_AND, value.get(), mask.get(), false) };
                    long long word = 0;
                    if (!low || !to_index(low.get(), word))
                        return false;
                    aWords.push_back(static_cast<std::uint32_t>(word));
                    value = ref{ binary(NEOS_PY_RSHIFT, value.get(), shift.get(), false) };
                    if (!value)
                        return false;
                    ref const done{ compare(NEOS_PY_EQ, value.get(), zero.get()) };
                    if (truth(done.get()) == 1)
                        break;
                }
                return true;
            }

            void seed_from_words(random_object& r, std::vector<std::uint32_t> aWords)
            {
                while (aWords.size() > 1u && aWords.back() == 0u)
                    aWords.pop_back();
                if (aWords.empty())
                    aWords.push_back(0u);
                r.state.init_by_array(aWords);
                release(r.gaussNext);
                r.gaussNext = nullptr;
            }

            bool seed(random_object& r, object* aSeed, long long aVersion)
            {
                if (aSeed == nullptr || kind_of(aSeed) == NEOS_PY_NONE)
                {
                    std::random_device device;
                    std::vector<std::uint32_t> words;
                    for (int i = 0; i < N; ++i)
                        words.push_back(device());
                    seed_from_words(r, words);
                    return true;
                }
                std::string_view bytes;
                if (kind_of(aSeed) == NEOS_PY_STR || bytes_like(aSeed, bytes))
                {
                    std::string data = kind_of(aSeed) == NEOS_PY_STR ? as<str_object>(aSeed)->value : std::string{ bytes };
                    if (aVersion == 1)
                    {
                        // (version 1: a hash of the str (latin-1))
                        auto const points = to_code_points(data);
                        std::uint64_t x = points.empty() ? 0u : static_cast<std::uint64_t>(points[0]) << 7;
                        for (auto c : points)
                            x = (1000003u * x) ^ c;
                        x ^= points.size();
                        ref const value{ make_int(static_cast<long long>(x)) };
                        std::vector<std::uint32_t> words{ static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(x >> 32) };
                        seed_from_words(r, words);
                        return true;
                    }
                    // (version 2: int.from_bytes(a + sha512(a).digest()))
                    ref const hashlib{ fn_import("hashlib") };
                    ref const sha512{ hashlib ? getattr(hashlib.get(), "sha512") : nullptr };
                    if (!sha512)
                        return false;
                    ref const hash{ call_with(sha512.get(), { make_bytes(data) }) };
                    ref const digest{ hash ? call_method_named(hash.get(), "digest", {}) : nullptr };
                    std::string_view digestBytes;
                    if (!digest || !bytes_like(digest.get(), digestBytes))
                        return false;
                    auto const all = data + std::string{ digestBytes };
                    // (big-endian bytes: the words from the right)
                    std::vector<std::uint32_t> words;
                    for (std::size_t end = all.size(); end > 0u;)
                    {
                        auto const start = end >= 4u ? end - 4u : 0u;
                        std::uint32_t word = 0u;
                        for (auto i = start; i < end; ++i)
                            word = (word << 8) | static_cast<unsigned char>(all[i]);
                        words.push_back(word);
                        end = start;
                    }
                    seed_from_words(r, words);
                    return true;
                }
                if (kind_of(builtin_value(aSeed)) == NEOS_PY_INT || kind_of(aSeed) == NEOS_PY_BOOL)
                {
                    std::vector<std::uint32_t> words;
                    if (!words_of(builtin_value(aSeed), words))
                        return false;
                    seed_from_words(r, words);
                    return true;
                }
                if (kind_of(aSeed) != NEOS_PY_FLOAT)
                {
                    error("TypeError", "The only supported seed types are:\nNone, int, float, str, bytes, and bytearray.");
                    return false;
                }
                long long h = 0;
                if (!hash_of(aSeed, h))
                    return false;
                auto const u = static_cast<std::uint64_t>(h);
                seed_from_words(r, { static_cast<std::uint32_t>(u), static_cast<std::uint32_t>(u >> 32) });
                return true;
            }

            // (getrandbits(k): an int)
            object* getrandbits(random_object& r, long long k)
            {
                if (k < 0)
                    return error("ValueError", "number of bits must be non-negative");
                if (k == 0)
                    return make_int(0);
                if (k <= 32)
                    return make_int(r.state.next() >> (32 - k));
                if (k <= 62)
                {
                    std::uint64_t value = r.state.next();
                    auto high = r.state.next();
                    high >>= (64 - k);
                    value |= static_cast<std::uint64_t>(high) << 32;
                    return make_int(static_cast<long long>(value));
                }
                // (words from least significant)
                ref result{ make_int(0) };
                long long shift = 0;
                for (auto remaining = k; remaining > 0; remaining -= 32, shift += 32)
                {
                    auto word = r.state.next();
                    if (remaining < 32)
                        word >>= (32 - remaining);
                    ref const w{ make_int(word) };
                    ref const s{ make_int(shift) };
                    ref const shifted{ binary(NEOS_PY_LSHIFT, w.get(), s.get(), false) };
                    result = ref{ binary(NEOS_PY_OR, result.get(), shifted.get(), false) };
                    if (!result)
                        return nullptr;
                }
                return result.release_ownership();
            }

            long long bit_length(unsigned long long n) { return n == 0u ? 0 : 64 - std::countl_zero(n); }

            // (_randbelow(n): 0 <= result < n)
            object* randbelow(random_object& r, object* aN)
            {
                long long n = 0;
                if (to_index(aN, n))
                {
                    if (n <= 0)
                        return make_int(0);
                    auto const k = bit_length(static_cast<unsigned long long>(n));
                    for (;;)
                    {
                        ref value{ getrandbits(r, k) };
                        long long v = 0;
                        if (!value || !to_index(value.get(), v))
                            return nullptr;
                        if (v < n)
                            return value.release_ownership();
                    }
                }
                clear_error();
                ref const bits{ call_method_named(aN, "bit_length", {}) };
                long long k = 0;
                if (!bits || !to_index(bits.get(), k))
                    return nullptr;
                for (;;)
                {
                    ref value{ getrandbits(r, k) };
                    if (!value)
                        return nullptr;
                    ref const less{ compare(NEOS_PY_LT, value.get(), aN) };
                    if (truth(less.get()) == 1)
                        return value.release_ownership();
                }
            }

            bool index_argument(object* aValue, ref& aResult)
            {
                if (kind_of(aValue) == NEOS_PY_FLOAT || (!is_int_like(builtin_value(aValue)) && !is_instance(aValue)))
                {
                    error("TypeError", std::string{ "'" } + type_name(aValue) + "' object cannot be interpreted as an integer");
                    return false;
                }
                long long n = 0;
                if (to_index(aValue, n))
                {
                    aResult = ref{ make_int(n) };
                    return true;
                }
                if (is_int_like(builtin_value(aValue)))
                {
                    clear_error();
                    aResult = ref::borrowed(builtin_value(aValue));
                    return true;
                }
                return false;
            }

            std::string text_of(object* aValue)
            {
                std::string text;
                str_of(aValue, text);
                return text;
            }

            object* randrange(random_object& r, object* aStart, object* aStop, object* aStep)
            {
                ref start;
                if (!index_argument(aStart, start))
                    return nullptr;
                if (aStop == nullptr || kind_of(aStop) == NEOS_PY_NONE)
                {
                    if (aStep != nullptr)
                        return error("TypeError", "Missing a non-None stop argument");
                    ref const zero{ make_int(0) };
                    ref const positive{ compare(NEOS_PY_GT, start.get(), zero.get()) };
                    if (truth(positive.get()) == 1)
                        return randbelow(r, start.get());
                    return error("ValueError", "empty range for randrange()");
                }
                ref stop;
                if (!index_argument(aStop, stop))
                    return nullptr;
                ref const width{ binary(NEOS_PY_SUBTRACT, stop.get(), start.get(), false) };
                ref step{ make_int(1) };
                if (aStep != nullptr && !index_argument(aStep, step))
                    return nullptr;
                ref const zero{ make_int(0) };
                long long istep = 0;
                to_index(step.get(), istep);
                clear_error();
                if (istep == 1)
                {
                    ref const positive{ compare(NEOS_PY_GT, width.get(), zero.get()) };
                    if (truth(positive.get()) == 1)
                    {
                        ref const offset{ randbelow(r, width.get()) };
                        return offset ? binary(NEOS_PY_ADD, start.get(), offset.get(), false) : nullptr;
                    }
                    return error("ValueError", "empty range in randrange(" + text_of(aStart) + ", " + text_of(aStop) + ")");
                }
                ref n;
                ref const one{ make_int(1) };
                ref const stepPositive{ compare(NEOS_PY_GT, step.get(), zero.get()) };
                ref const stepNegative{ compare(NEOS_PY_LT, step.get(), zero.get()) };
                if (truth(stepPositive.get()) == 1)
                {
                    ref const a{ binary(NEOS_PY_ADD, width.get(), step.get(), false) };
                    ref const b{ binary(NEOS_PY_SUBTRACT, a.get(), one.get(), false) };
                    n = ref{ binary(NEOS_PY_FLOOR_DIVIDE, b.get(), step.get(), false) };
                }
                else if (truth(stepNegative.get()) == 1)
                {
                    ref const a{ binary(NEOS_PY_ADD, width.get(), step.get(), false) };
                    ref const b{ binary(NEOS_PY_ADD, a.get(), one.get(), false) };
                    n = ref{ binary(NEOS_PY_FLOOR_DIVIDE, b.get(), step.get(), false) };
                }
                else
                    return error("ValueError", "zero step for randrange()");
                ref const empty{ compare(NEOS_PY_LE, n.get(), zero.get()) };
                if (truth(empty.get()) == 1)
                    return error("ValueError", "empty range in randrange(" + text_of(aStart) + ", " + text_of(aStop) + ", " + text_of(aStep) + ")");
                ref const k{ randbelow(r, n.get()) };
                if (!k)
                    return nullptr;
                ref const scaled{ binary(NEOS_PY_MULTIPLY, step.get(), k.get(), false) };
                return binary(NEOS_PY_ADD, start.get(), scaled.get(), false);
            }

            double normalvariate(random_object& r, double aMu, double aSigma)
            {
                double const magic = 4.0 * std::exp(-0.5) / std::sqrt(2.0);
                double z = 0.0;
                for (;;)
                {
                    auto const u1 = r.state.random();
                    auto const u2 = 1.0 - r.state.random();
                    z = magic * (u1 - 0.5) / u2;
                    auto const zz = z * z / 4.0;
                    if (zz <= -std::log(u2))
                        break;
                }
                return aMu + z * aSigma;
            }

            bool gammavariate(random_object& r, double aAlpha, double aBeta, double& aResult)
            {
                if (aAlpha <= 0.0 || aBeta <= 0.0)
                {
                    error("ValueError", "gammavariate: alpha and beta must be > 0.0");
                    return false;
                }
                if (aAlpha > 1.0)
                {
                    auto const ainv = std::sqrt(2.0 * aAlpha - 1.0);
                    auto const bbb = aAlpha - std::log(4.0);
                    auto const ccc = aAlpha + ainv;
                    for (;;)
                    {
                        auto const u1 = r.state.random();
                        if (!(1e-07 < u1 && u1 < 0.9999999))
                            continue;
                        auto const u2 = 1.0 - r.state.random();
                        auto const v = std::log(u1 / (1.0 - u1)) / ainv;
                        auto const x = aAlpha * std::exp(v);
                        auto const z = u1 * u1 * u2;
                        auto const rr = bbb + ccc * v - x;
                        if (rr + (1.0 + std::log(4.5)) - 4.5 * z >= 0.0 || rr >= std::log(z))
                        {
                            aResult = x * aBeta;
                            return true;
                        }
                    }
                }
                if (aAlpha == 1.0)
                {
                    aResult = -std::log(1.0 - r.state.random()) * aBeta;
                    return true;
                }
                double x = 0.0;
                for (;;)
                {
                    auto const u = r.state.random();
                    auto const b = (std::numbers::e + aAlpha) / std::numbers::e;
                    auto const p = b * u;
                    x = p <= 1.0 ? std::pow(p, 1.0 / aAlpha) : -std::log((b - p) / aAlpha);
                    auto const u1 = r.state.random();
                    if (p > 1.0)
                    {
                        if (u1 <= std::pow(x, aAlpha - 1.0))
                            break;
                    }
                    else if (u1 <= std::exp(-x))
                        break;
                }
                aResult = x * aBeta;
                return true;
            }

            // (a sequence's items: for choice, sample and choices)
            bool sequence_length(object* aSequence, long long& aLength)
            {
                aLength = length(aSequence);
                return aLength >= 0;
            }

            object* item_at(object* aSequence, long long aIndex)
            {
                ref const index{ make_int(aIndex) };
                return getitem(aSequence, index.get());
            }

            using method = object* (*)(random_object& r, std::vector<object*> const& aArgs, object* aKwargs);

            struct random_method { char const* name; method function; };

            random_method const sMethods[] =
            {
                { "seed", [](random_object& r, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("seed", aArgs, aKwargs, { "a", "version" }, 0u, 0u, a))
                            return nullptr;
                        long long version = 2;
                        if (a[1] != nullptr && !to_index(a[1], version))
                            return nullptr;
                        return seed(r, a[0], version) ? borrowed(none()) : nullptr;
                    } },
                { "random", [](random_object& r, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        return parse_arguments("random", aArgs, aKwargs, {}, 0u, 0u, a) ? make_float(r.state.random()) : nullptr;
                    } },
                { "getrandbits", [](random_object& r, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long k = 0;
                        if (!parse_arguments("getrandbits", aArgs, aKwargs, { "k" }, 1u, 1u, a) || !to_index(a[0], k))
                            return nullptr;
                        return getrandbits(r, k);
                    } },
                { "randbytes", [](random_object& r, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long n = 0;
                        if (!parse_arguments("randbytes", aArgs, aKwargs, { "n" }, 1u, 0u, a) || !to_index(a[0], n))
                            return nullptr;
                        if (n < 0)
                            return error("ValueError", "number of bits must be non-negative");
                        // (getrandbits(n * 8).to_bytes(n, 'little'))
                        std::string result;
                        for (auto remaining = n * 8; remaining > 0; remaining -= 32)
                        {
                            auto word = r.state.next();
                            if (remaining < 32)
                                word >>= (32 - remaining);
                            for (int i = 0; i < 4 && static_cast<long long>(result.size()) < n; ++i)
                                result += static_cast<char>(word >> (8 * i));
                        }
                        return make_bytes(result);
                    } },
                { "randrange", [](random_object& r, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("randrange", aArgs, aKwargs, { "start", "stop", "step" }, 1u, 0u, a))
                            return nullptr;
                        return randrange(r, a[0], a[1], a[2]);
                    } },
                { "randint", [](random_object& r, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("randint", aArgs, aKwargs, { "a", "b" }, 2u, 0u, a))
                            return nullptr;
                        ref const one{ make_int(1) };
                        ref const stop{ binary(NEOS_PY_ADD, a[1], one.get(), false) };
                        return stop ? randrange(r, a[0], stop.get(), nullptr) : nullptr;
                    } },
                { "choice", [](random_object& r, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long n = 0;
                        if (!parse_arguments("choice", aArgs, aKwargs, { "seq" }, 1u, 0u, a) || !sequence_length(a[0], n))
                            return nullptr;
                        if (n == 0)
                            return error("IndexError", "Cannot choose from an empty sequence");
                        ref const size{ make_int(n) };
                        ref const index{ randbelow(r, size.get()) };
                        return index ? getitem(a[0], index.get()) : nullptr;
                    } },
                { "shuffle", [](random_object& r, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long n = 0;
                        if (!parse_arguments("shuffle", aArgs, aKwargs, { "x" }, 1u, 0u, a) || !sequence_length(a[0], n))
                            return nullptr;
                        for (auto i = n - 1; i >= 1; --i)
                        {
                            ref const bound{ make_int(i + 1) };
                            ref const jObject{ randbelow(r, bound.get()) };
                            long long j = 0;
                            if (!jObject || !to_index(jObject.get(), j))
                                return nullptr;
                            ref const x{ item_at(a[0], i) };
                            ref const y{ item_at(a[0], j) };
                            ref const ii{ make_int(i) };
                            ref const jj{ make_int(j) };
                            if (!x || !y || setitem(a[0], ii.get(), y.get()) < 0 || setitem(a[0], jj.get(), x.get()) < 0)
                                return nullptr;
                        }
                        return borrowed(none());
                    } },
                { "sample", [](random_object& r, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (aArgs.size() > 2u)
                            return error("TypeError", "Random.sample() takes 3 positional arguments but " + std::to_string(aArgs.size() + 1u) + " were given");
                        if (!parse_arguments("sample", aArgs, aKwargs, { "population", "k", "counts" }, 2u, 0u, a))
                            return nullptr;
                        auto const population = a[0];
                        auto const k0 = kind_of(builtin_value(population));
                        bool const sequence = k0 == NEOS_PY_LIST || k0 == NEOS_PY_TUPLE || k0 == NEOS_PY_STR || k0 == NEOS_PY_RANGE ||
                            (python_type(population) != nullptr && (std::string_view{ population->type->name } == "collections.deque" ||
                            std::string_view{ population->type->name } == "bytes" || std::string_view{ population->type->name } == "bytearray")) ||
                            (is_instance(population) && has_special_method(population, "__getitem__") && has_special_method(population, "__len__"));
                        if (!sequence)
                            return error("TypeError", "Population must be a sequence.  For dicts or sets, use sorted(d).");
                        long long n = 0, k = 0;
                        if (!sequence_length(population, n) || !to_index(a[1], k))
                            return nullptr;
                        if (a[2] != nullptr && kind_of(a[2]) != NEOS_PY_NONE)
                        {
                            // (counts: a sample of range(total), mapped by bisect)
                            ref const itertools{ fn_import("itertools") };
                            ref const accumulate{ getattr(itertools.get(), "accumulate") };
                            ref const accumulated{ accumulate ? call_with(accumulate.get(), { borrowed(a[2]) }) : nullptr };
                            ref cumulative{ accumulated ? b_list({ accumulated.get() }, nullptr) : nullptr };
                            if (!cumulative)
                                return nullptr;
                            auto& items = as<list_object>(cumulative.get())->items;
                            if (static_cast<long long>(items.size()) != n)
                                return error("ValueError", "The number of counts does not match the population");
                            ref total{ items.empty() ? make_int(0) : items.back() };
                            if (!items.empty())
                                items.pop_back();
                            if (!is_int_like(total.get()))
                                return error("TypeError", "Counts must be integers");
                            long long t = 0;
                            to_index(total.get(), t);
                            if (t < 0)
                                return error("ValueError", "Counts must be non-negative");
                            ref const range{ make_range(0, t, 1) };
                            ref const self{ borrowed(&r.h.base) };
                            ref const kwargs{ make_dict() };
                            ref const kKey{ make_str("k") };
                            dict_set(kwargs.get(), kKey.get(), a[1]);
                            ref const sampleMethod{ getattr(self.get(), "sample") };
                            ref const selections{ call_with(sampleMethod.get(), { borrowed(range.get()) }, kwargs.get()) };
                            if (!selections)
                                return nullptr;
                            ref const bisectModule{ fn_import("bisect") };
                            ref const bisect{ getattr(bisectModule.get(), "bisect") };
                            std::vector<object*> result;
                            for (auto s : as<list_object>(selections.get())->items)
                            {
                                ref const index{ call_with(bisect.get(), { borrowed(cumulative.get()), borrowed(s) }) };
                                auto const item = index ? getitem(population, index.get()) : nullptr;
                                if (item == nullptr)
                                {
                                    for (auto x : result)
                                        release(x);
                                    return nullptr;
                                }
                                result.push_back(item);
                            }
                            return make_list(std::move(result));
                        }
                        if (k < 0 || k > n)
                            return error("ValueError", "Sample larger than population or is negative");
                        std::vector<object*> result(static_cast<std::size_t>(k), nullptr);
                        long long setsize = 21;
                        if (k > 5)
                            setsize += static_cast<long long>(std::pow(4.0, std::ceil(std::log(static_cast<double>(k * 3)) / std::log(4.0))));
                        auto const cleanup = [&]() { for (auto x : result) release(x); };
                        if (n <= setsize)
                        {
                            std::vector<object*> pool;
                            if (!collect(population, pool))
                                return nullptr;
                            for (long long i = 0; i < k; ++i)
                            {
                                ref const bound{ make_int(n - i) };
                                ref const jObject{ randbelow(r, bound.get()) };
                                long long j = 0;
                                if (!jObject || !to_index(jObject.get(), j))
                                {
                                    cleanup();
                                    for (auto p : pool)
                                        release(p);
                                    return nullptr;
                                }
                                result[static_cast<std::size_t>(i)] = borrowed(pool[static_cast<std::size_t>(j)]);
                                std::swap(pool[static_cast<std::size_t>(j)], pool[static_cast<std::size_t>(n - i - 1)]);
                            }
                            for (auto p : pool)
                                release(p);
                        }
                        else
                        {
                            std::set<long long> selected;
                            ref const bound{ make_int(n) };
                            for (long long i = 0; i < k; ++i)
                            {
                                long long j = 0;
                                do
                                {
                                    ref const jObject{ randbelow(r, bound.get()) };
                                    if (!jObject || !to_index(jObject.get(), j))
                                    {
                                        cleanup();
                                        return nullptr;
                                    }
                                } while (selected.contains(j));
                                selected.insert(j);
                                result[static_cast<std::size_t>(i)] = item_at(population, j);
                                if (result[static_cast<std::size_t>(i)] == nullptr)
                                {
                                    cleanup();
                                    return nullptr;
                                }
                            }
                        }
                        return make_list(std::move(result));
                    } },
                { "choices", [](random_object& r, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (aArgs.size() > 2u)
                            return error("TypeError", "Random.choices() takes from 2 to 3 positional arguments but " + std::to_string(aArgs.size() + 1u) + " were given");
                        if (!parse_arguments("choices", aArgs, aKwargs, { "population", "weights", "cum_weights", "k" }, 1u, 0u, a))
                            return nullptr;
                        long long n = 0, k = 1;
                        if (!sequence_length(a[0], n) || (a[3] != nullptr && !to_index(a[3], k)))
                            return nullptr;
                        std::vector<object*> result;
                        auto const cleanup = [&]() { for (auto x : result) release(x); };
                        bool const noWeights = a[1] == nullptr || kind_of(a[1]) == NEOS_PY_NONE;
                        bool const noCumulative = a[2] == nullptr || kind_of(a[2]) == NEOS_PY_NONE;
                        if (noCumulative && noWeights)
                        {
                            auto const nf = static_cast<double>(n);
                            for (long long i = 0; i < k; ++i)
                            {
                                auto const item = item_at(a[0], static_cast<long long>(std::floor(r.state.random() * nf)));
                                if (item == nullptr)
                                {
                                    cleanup();
                                    return nullptr;
                                }
                                result.push_back(item);
                            }
                            return make_list(std::move(result));
                        }
                        ref cumulative;
                        if (noCumulative)
                        {
                            if (is_int_like(a[1]))
                            {
                                std::string text;
                                repr_of(a[1], text);
                                return error("TypeError", "The number of choices must be a keyword argument: k=" + text);
                            }
                            ref const itertools{ fn_import("itertools") };
                            ref const accumulate{ getattr(itertools.get(), "accumulate") };
                            ref const accumulated{ accumulate ? call_with(accumulate.get(), { borrowed(a[1]) }) : nullptr };
                            cumulative = ref{ accumulated ? b_list({ accumulated.get() }, nullptr) : nullptr };
                            if (!cumulative)
                                return nullptr;
                        }
                        else if (!noWeights)
                            return error("TypeError", "Cannot specify both weights and cumulative weights");
                        else
                        {
                            cumulative = ref{ b_list({ a[2] }, nullptr) };
                            if (!cumulative)
                                return nullptr;
                        }
                        auto const& weights = as<list_object>(cumulative.get())->items;
                        if (static_cast<long long>(weights.size()) != n)
                            return error("ValueError", "The number of weights does not match the population");
                        double total = 0.0;
                        if (weights.empty() || !to_double(weights.back(), total))
                            return failed() ? nullptr : error("IndexError", "list index out of range");
                        if (total <= 0.0)
                            return error("ValueError", "Total of weights must be greater than zero");
                        if (!std::isfinite(total))
                            return error("ValueError", "Total of weights must be finite");
                        ref const bisectModule{ fn_import("bisect") };
                        ref const bisect{ getattr(bisectModule.get(), "bisect") };
                        for (long long i = 0; i < k; ++i)
                        {
                            ref const index{ call_with(bisect.get(), { borrowed(cumulative.get()), make_float(r.state.random() * total), make_int(0), make_int(n - 1) }) };
                            auto const item = index ? getitem(a[0], index.get()) : nullptr;
                            if (item == nullptr)
                            {
                                cleanup();
                                return nullptr;
                            }
                            result.push_back(item);
                        }
                        return make_list(std::move(result));
                    } },
                { "uniform", [](random_object& r, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("uniform", aArgs, aKwargs, { "a", "b" }, 2u, 0u, a))
                            return nullptr;
                        // (a + (b - a) * random(): as Python's arithmetic)
                        ref const width{ binary(NEOS_PY_SUBTRACT, a[1], a[0], false) };
                        ref const u{ make_float(r.state.random()) };
                        ref const scaled{ width ? binary(NEOS_PY_MULTIPLY, width.get(), u.get(), false) : nullptr };
                        return scaled ? binary(NEOS_PY_ADD, a[0], scaled.get(), false) : nullptr;
                    } },
                { "triangular", [](random_object& r, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("triangular", aArgs, aKwargs, { "low", "high", "mode" }, 0u, 0u, a))
                            return nullptr;
                        double low = 0.0, high = 1.0, mode = 0.0;
                        if ((a[0] != nullptr && !to_double(a[0], low)) || (a[1] != nullptr && !to_double(a[1], high)))
                            return nullptr;
                        auto u = r.state.random();
                        double c = 0.5;
                        if (a[2] != nullptr && kind_of(a[2]) != NEOS_PY_NONE)
                        {
                            if (!to_double(a[2], mode))
                                return nullptr;
                            if (high == low)
                                return make_float(low);
                            c = (mode - low) / (high - low);
                        }
                        else if (high == low)
                            return make_float(low);
                        if (u > c)
                        {
                            u = 1.0 - u;
                            c = 1.0 - c;
                            std::swap(low, high);
                        }
                        return make_float(low + (high - low) * std::sqrt(u * c));
                    } },
                { "normalvariate", [](random_object& r, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        double mu = 0.0, sigma = 1.0;
                        if (!parse_arguments("normalvariate", aArgs, aKwargs, { "mu", "sigma" }, 0u, 0u, a) || (a[0] != nullptr && !to_double(a[0], mu)) ||
                            (a[1] != nullptr && !to_double(a[1], sigma)))
                            return nullptr;
                        return make_float(normalvariate(r, mu, sigma));
                    } },
                { "gauss", [](random_object& r, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        double mu = 0.0, sigma = 1.0;
                        if (!parse_arguments("gauss", aArgs, aKwargs, { "mu", "sigma" }, 0u, 0u, a) || (a[0] != nullptr && !to_double(a[0], mu)) ||
                            (a[1] != nullptr && !to_double(a[1], sigma)))
                            return nullptr;
                        double z = 0.0;
                        if (r.gaussNext == nullptr)
                        {
                            auto const x2pi = r.state.random() * 2.0 * std::numbers::pi;
                            auto const g2rad = std::sqrt(-2.0 * std::log(1.0 - r.state.random()));
                            z = std::cos(x2pi) * g2rad;
                            r.gaussNext = make_float(std::sin(x2pi) * g2rad);
                        }
                        else
                        {
                            to_double(r.gaussNext, z);
                            release(r.gaussNext);
                            r.gaussNext = nullptr;
                        }
                        return make_float(mu + z * sigma);
                    } },
                { "lognormvariate", [](random_object& r, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        double mu = 0.0, sigma = 0.0;
                        if (!parse_arguments("lognormvariate", aArgs, aKwargs, { "mu", "sigma" }, 2u, 0u, a) || !to_double(a[0], mu) || !to_double(a[1], sigma))
                            return nullptr;
                        return make_float(std::exp(normalvariate(r, mu, sigma)));
                    } },
                { "expovariate", [](random_object& r, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        double lambd = 1.0;
                        if (!parse_arguments("expovariate", aArgs, aKwargs, { "lambd" }, 0u, 0u, a) || (a[0] != nullptr && !to_double(a[0], lambd)))
                            return nullptr;
                        if (lambd == 0.0)
                            return error("ZeroDivisionError", "float division by zero");
                        return make_float(-std::log(1.0 - r.state.random()) / lambd);
                    } },
                { "vonmisesvariate", [](random_object& r, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        double mu = 0.0, kappa = 0.0;
                        if (!parse_arguments("vonmisesvariate", aArgs, aKwargs, { "mu", "kappa" }, 2u, 0u, a) || !to_double(a[0], mu) || !to_double(a[1], kappa))
                            return nullptr;
                        auto const twoPi = 2.0 * std::numbers::pi;
                        if (kappa <= 1e-06)
                            return make_float(twoPi * r.state.random());
                        auto const s = 0.5 / kappa;
                        auto const rr = s + std::sqrt(1.0 + s * s);
                        double z = 0.0;
                        for (;;)
                        {
                            auto const u1 = r.state.random();
                            z = std::cos(std::numbers::pi * u1);
                            auto const d = z / (rr + z);
                            auto const u2 = r.state.random();
                            if (u2 < 1.0 - d * d || u2 <= (1.0 - d) * std::exp(d))
                                break;
                        }
                        auto const q = 1.0 / rr;
                        auto const f = (q + z) / (1.0 + q * z);
                        auto const u3 = r.state.random();
                        auto const theta = u3 > 0.5 ? std::fmod(mu + std::acos(f), twoPi) : std::fmod(mu - std::acos(f), twoPi);
                        return make_float(theta < 0.0 ? theta + twoPi : theta);
                    } },
                { "gammavariate", [](random_object& r, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        double alpha = 0.0, beta = 0.0, result = 0.0;
                        if (!parse_arguments("gammavariate", aArgs, aKwargs, { "alpha", "beta" }, 2u, 0u, a) || !to_double(a[0], alpha) || !to_double(a[1], beta) ||
                            !gammavariate(r, alpha, beta, result))
                            return nullptr;
                        return make_float(result);
                    } },
                { "betavariate", [](random_object& r, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        double alpha = 0.0, beta = 0.0, y = 0.0, z = 0.0;
                        if (!parse_arguments("betavariate", aArgs, aKwargs, { "alpha", "beta" }, 2u, 0u, a) || !to_double(a[0], alpha) || !to_double(a[1], beta) ||
                            !gammavariate(r, alpha, 1.0, y))
                            return nullptr;
                        if (y == 0.0)
                            return make_float(0.0);
                        if (!gammavariate(r, beta, 1.0, z))
                            return nullptr;
                        return make_float(y / (y + z));
                    } },
                { "paretovariate", [](random_object& r, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        double alpha = 0.0;
                        if (!parse_arguments("paretovariate", aArgs, aKwargs, { "alpha" }, 1u, 0u, a) || !to_double(a[0], alpha))
                            return nullptr;
                        auto const u = 1.0 - r.state.random();
                        return make_float(std::pow(u, -1.0 / alpha));
                    } },
                { "weibullvariate", [](random_object& r, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        double alpha = 0.0, beta = 0.0;
                        if (!parse_arguments("weibullvariate", aArgs, aKwargs, { "alpha", "beta" }, 2u, 0u, a) || !to_double(a[0], alpha) || !to_double(a[1], beta))
                            return nullptr;
                        auto const u = 1.0 - r.state.random();
                        return make_float(alpha * std::pow(-std::log(u), 1.0 / beta));
                    } },
                { "binomialvariate", [](random_object& r, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long n = 1;
                        double p = 0.5;
                        if (!parse_arguments("binomialvariate", aArgs, aKwargs, { "n", "p" }, 0u, 0u, a) || (a[0] != nullptr && !to_index(a[0], n)) ||
                            (a[1] != nullptr && !to_double(a[1], p)))
                            return nullptr;
                        if (n < 0)
                            return error("ValueError", "n must be non-negative");
                        if (p <= 0.0 || p >= 1.0)
                        {
                            if (p == 0.0)
                                return make_int(0);
                            if (p == 1.0)
                                return make_int(n);
                            return error("ValueError", "p must be in the range 0.0 <= p <= 1.0");
                        }
                        // (BTRS: binomialvariate(n, 1 - p) when p > 0.5)
                        bool flip = false;
                        if (n == 1)
                            return make_int(r.state.random() < p ? 1 : 0);
                        if (p > 0.5)
                        {
                            flip = true;
                            p = 1.0 - p;
                        }
                        auto const finish = [&](long long k) { return make_int(flip ? n - k : k); };
                        if (static_cast<double>(n) * p < 10.0)
                        {
                            long long x = 0, y = 0;
                            auto const c = std::log2(1.0 - p);
                            if (c == 0.0)
                                return finish(x);
                            for (;;)
                            {
                                auto const u = r.state.random();
                                if (u == 0.0)
                                    continue;
                                y += static_cast<long long>(std::floor(std::log2(u) / c)) + 1;
                                if (y > n)
                                    return finish(x);
                                ++x;
                            }
                        }
                        bool setup = false;
                        auto const spq = std::sqrt(static_cast<double>(n) * p * (1.0 - p));
                        auto const b = 1.15 + 2.53 * spq;
                        auto const aa = -0.0873 + 0.0248 * b + 0.01 * p;
                        auto const c = static_cast<double>(n) * p + 0.5;
                        auto const vr = 0.92 - 4.2 / b;
                        double alpha = 0.0, lpq = 0.0, h = 0.0;
                        long long m = 0;
                        for (;;)
                        {
                            auto u = r.state.random();
                            u -= 0.5;
                            auto const us = 0.5 - std::fabs(u);
                            if (us == 0.0)
                                continue;
                            auto const k = static_cast<long long>(std::floor((2.0 * aa / us + b) * u + c));
                            if (k < 0 || k > n)
                                continue;
                            auto v = r.state.random();
                            if (us >= 0.07 && v <= vr)
                                return finish(k);
                            if (!setup)
                            {
                                alpha = (2.83 + 5.1 / b) * spq;
                                lpq = std::log(p / (1.0 - p));
                                m = static_cast<long long>(std::floor(static_cast<double>(n + 1) * p));
                                h = std::lgamma(static_cast<double>(m + 1)) + std::lgamma(static_cast<double>(n - m + 1));
                                setup = true;
                            }
                            v *= alpha / (aa / (us * us) + b);
                            if (std::log(v) <= h - std::lgamma(static_cast<double>(k + 1)) - std::lgamma(static_cast<double>(n - k + 1)) + static_cast<double>(k - m) * lpq)
                                return finish(k);
                        }
                    } },
                { "getstate", [](random_object& r, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("getstate", aArgs, aKwargs, {}, 0u, 0u, a))
                            return nullptr;
                        std::vector<object*> words;
                        for (auto w : r.state.mt)
                            words.push_back(make_int(w));
                        words.push_back(make_int(r.state.index));
                        return make_tuple({ make_int(3), make_tuple(std::move(words)), borrowed(r.gaussNext != nullptr ? r.gaussNext : none()) });
                    } },
                { "setstate", [](random_object& r, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("setstate", aArgs, aKwargs, { "state" }, 1u, 0u, a))
                            return nullptr;
                        if (kind_of(a[0]) != NEOS_PY_TUPLE || as<tuple_object>(a[0])->items.size() != 3u)
                            return error("ValueError", "state with version 3 passed to Random.setstate() of version 3");
                        auto const& state = as<tuple_object>(a[0])->items;
                        if (kind_of(state[1]) != NEOS_PY_TUPLE || as<tuple_object>(state[1])->items.size() != N + 1)
                            return error("ValueError", "state vector is the wrong size");
                        auto const& words = as<tuple_object>(state[1])->items;
                        for (int i = 0; i < N; ++i)
                        {
                            long long w = 0;
                            if (!to_index(words[static_cast<std::size_t>(i)], w))
                                return nullptr;
                            r.state.mt[static_cast<std::size_t>(i)] = static_cast<std::uint32_t>(w);
                        }
                        long long index = 0;
                        if (!to_index(words[N], index))
                            return nullptr;
                        if (index < 0 || index > N)
                            return error("ValueError", "invalid state");
                        r.state.index = static_cast<int>(index);
                        release(r.gaussNext);
                        r.gaussNext = kind_of(state[2]) == NEOS_PY_NONE ? nullptr : borrowed(state[2]);
                        return borrowed(none());
                    } }
            };

            object* call_random_method(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
            {
                auto const& m = *static_cast<random_method const*>(aData);
                auto const& args = as<tuple_object>(aArgs)->items;
                if (args.empty() || args[0]->type != &sRandomType.base)
                    return error("TypeError", std::string{ "descriptor '" } + m.name + "' requires a 'random.Random' object");
                std::vector<object*> const rest{ args.begin() + 1, args.end() };
                return m.function(as_random(args[0]), rest, aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty() ? aKwargs : nullptr);
            }

            neos_py_type make_random_type()
            {
                auto type = make_type("random.Random", "Random", retain_object, release_object<random_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object* { return make_str("<random.Random object at " + object_address(aSelf) + ">"); };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    for (auto const& m : sMethods)
                        if (std::strcmp(m.name, aName) == 0)
                            return make_function(m.name, call_random_method, const_cast<random_method*>(&m), aSelf);
                    if (std::strcmp(aName, "gauss_next") == 0)
                        return borrowed(as_random(aSelf).gaussNext != nullptr ? as_random(aSelf).gaussNext : none());
                    if (std::strcmp(aName, "VERSION") == 0)
                        return make_int(3);
                    return error("AttributeError", std::string{ "'Random' object has no attribute '" } + aName + "'");
                };
                return type;
            }

            object* new_random(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!parse_arguments("Random", as<tuple_object>(aArgs)->items, aKwargs, { "x" }, 0u, 1u, a))
                    return nullptr;
                ref result{ &(new random_object{ { { &sRandomType.base } } })->h.base };
                if (!seed(as_random(result.get()), a[0], 2))
                    return nullptr;
                return result.release_ownership();
            }

            int random_instancecheck(neos_py_context*, void*, object* aObject) { return aObject->type == &sRandomType.base ? 1 : 0; }

            // (SystemRandom: os.urandom's randomness)
            object* new_system_random(neos_py_context*, void*, object*, object*)
            {
                ref result{ &(new random_object{ { { &sRandomType.base } } })->h.base };
                seed(as_random(result.get()), nullptr, 2);
                return result.release_ownership();
            }

            bool random_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                // (the module's functions: the methods of a hidden instance)
                ref const instance{ &(new random_object{ { { &sRandomType.base } } })->h.base };
                seed(as_random(instance.get()), nullptr, 2);
                for (auto const& m : sMethods)
                    aAttributes.emplace_back(m.name, make_function(m.name, call_random_method, const_cast<random_method*>(&m), instance.get()));
                aAttributes.emplace_back("Random", context()->type_new(context(), "Random", "random.Random", new_random, nullptr, random_instancecheck));
                aAttributes.emplace_back("SystemRandom", context()->type_new(context(), "SystemRandom", "random.SystemRandom", new_system_random, nullptr, random_instancecheck));
                aAttributes.emplace_back("_inst", borrowed(instance.get()));
                return true;
            }

            inline bool const sRandom = register_module("random", random_module);
        }
    }
}
