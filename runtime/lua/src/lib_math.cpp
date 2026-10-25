/*
  lib_math.cpp

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

// The math library (math.random: the xoshiro256** generator)
//
// math.random's seeding and projection follow the Lua reference implementation's lmathlib.c (Copyright (c) 1994-2023 Lua.org,
// PUC-Rio, MIT licence; see runtime/lua/NOTICE).

#include <chrono>
#include <cmath>
#include <limits>
#include <numbers>
#include "lua.hpp"
#include "api.hpp"

namespace neos::lua_runtime
{
    namespace
    {
        value float_or_integer(double aValue)
        {
            // (a float with an integer value (in range): the integer)
            std::int64_t i;
            if (float_to_integer(aValue, i))
                return make_integer(i);
            return make_float(aValue);
        }

        void math_abs(native_call& c)
        {
            auto const n = check_number_value(c, 0u);
            if (is_integer(n))
            {
                auto const i = integer_of(n);
                c.ret(make_integer(i < 0 ? static_cast<std::int64_t>(0u - static_cast<std::uint64_t>(i)) : i));
            }
            else
                c.ret(make_float(std::fabs(float_of(n))));
        }

        void math_floor(native_call& c)
        {
            auto const n = check_number_value(c, 0u);
            if (is_integer(n))
                c.ret(n);
            else
                c.ret(float_or_integer(std::floor(float_of(n))));
        }

        void math_ceil(native_call& c)
        {
            auto const n = check_number_value(c, 0u);
            if (is_integer(n))
                c.ret(n);
            else
                c.ret(float_or_integer(std::ceil(float_of(n))));
        }

        void math_fmod(native_call& c)
        {
            auto const a = check_number_value(c, 0u);
            auto const b = check_number_value(c, 1u);
            if (is_integer(a) && is_integer(b))
            {
                auto const d = integer_of(b);
                if (static_cast<std::uint64_t>(d) + 1u <= 1u)
                {
                    // (0 or -1)
                    if (d == 0)
                        argument_error(c, 1u, "zero");
                    c.ret(make_integer(0));
                    return;
                }
                c.ret(make_integer(integer_of(a) % d));
                return;
            }
            c.ret(make_float(std::fmod(number_of(a), number_of(b))));
        }

        void math_modf(native_call& c)
        {
            auto const n = check_number_value(c, 0u);
            if (is_integer(n))
            {
                c.ret(n);
                c.ret(make_float(0.0));
                return;
            }
            auto const d = float_of(n);
            auto const ip = d < 0.0 ? std::ceil(d) : std::floor(d);
            c.ret(float_or_integer(ip));
            c.ret(make_float(ip == d ? 0.0 : d - ip));
        }

        void math_sqrt(native_call& c) { c.ret(make_float(std::sqrt(check_number(c, 0u)))); }
        void math_sin(native_call& c) { c.ret(make_float(std::sin(check_number(c, 0u)))); }
        void math_cos(native_call& c) { c.ret(make_float(std::cos(check_number(c, 0u)))); }
        void math_tan(native_call& c) { c.ret(make_float(std::tan(check_number(c, 0u)))); }
        void math_asin(native_call& c) { c.ret(make_float(std::asin(check_number(c, 0u)))); }
        void math_acos(native_call& c) { c.ret(make_float(std::acos(check_number(c, 0u)))); }
        void math_exp(native_call& c) { c.ret(make_float(std::exp(check_number(c, 0u)))); }

        void math_atan(native_call& c)
        {
            auto const y = check_number(c, 0u);
            auto const x = c.none(1u) || is_nil(c.arg(1u)) ? 1.0 : check_number(c, 1u);
            c.ret(make_float(std::atan2(y, x)));
        }

        void math_log(native_call& c)
        {
            auto const x = check_number(c, 0u);
            if (c.none(1u) || is_nil(c.arg(1u)))
            {
                c.ret(make_float(std::log(x)));
                return;
            }
            auto const base = check_number(c, 1u);
            if (base == 2.0)
                c.ret(make_float(std::log2(x)));
            else if (base == 10.0)
                c.ret(make_float(std::log10(x)));
            else
                c.ret(make_float(std::log(x) / std::log(base)));
        }

        void math_deg(native_call& c) { c.ret(make_float(check_number(c, 0u) * (180.0 / std::numbers::pi))); }
        void math_rad(native_call& c) { c.ret(make_float(check_number(c, 0u) * (std::numbers::pi / 180.0))); }

        void math_tointeger(native_call& c)
        {
            std::int64_t i;
            auto const v = c.arg(0u);
            if ((is_number(v) || is_string(v)) && to_integer(v, i, true))
                c.ret(make_integer(i));
            else
            {
                check_any(c, 0u);
                c.ret(Nil);
            }
        }

        void math_type(native_call& c)
        {
            check_any(c, 0u);
            auto const v = c.arg(0u);
            if (is_integer(v))
                c.ret(intern("integer"));
            else if (is_float(v))
                c.ret(intern("float"));
            else
                c.ret(Nil);
        }

        void math_ult(native_call& c)
        {
            auto const a = check_integer(c, 0u);
            auto const b = check_integer(c, 1u);
            c.ret(boolean(static_cast<std::uint64_t>(a) < static_cast<std::uint64_t>(b)));
        }

        void math_min_max(native_call& c, bool aMax)
        {
            auto best = check_number_value(c, 0u);
            for (std::size_t i = 1u; i < c.count(); ++i)
            {
                auto const v = check_number_value(c, i);
                if (aMax ? less_than(best, v) : less_than(v, best))
                    best = v;
            }
            c.ret(best);
        }

        void math_max(native_call& c) { math_min_max(c, true); }
        void math_min(native_call& c) { math_min_max(c, false); }

        // --- random numbers: xoshiro256**

        std::uint64_t rotate_left(std::uint64_t x, int n)
        {
            return (x << n) | (x >> (64 - n));
        }

        std::uint64_t next_random()
        {
            auto& s = the_state().randomState;
            auto const result = rotate_left(s[1] * 5u, 7) * 9u;
            auto const t = s[1] << 17;
            s[2] ^= s[0];
            s[3] ^= s[1];
            s[1] ^= s[2];
            s[0] ^= s[3];
            s[2] ^= t;
            s[3] = rotate_left(s[3], 45);
            return result;
        }

        void seed_random(std::uint64_t a, std::uint64_t b)
        {
            auto& s = the_state().randomState;
            s[0] = a;
            s[1] = 0xFFu;
            s[2] = b;
            s[3] = 0u;
            for (int i = 0; i < 16; ++i)
                next_random();
        }

        // (a random integer in [0, n]: by rejection of the values beyond the smallest mask of all ones that covers n)
        std::uint64_t project(std::uint64_t aRandom, std::uint64_t n)
        {
            if ((n & (n + 1u)) == 0u)
                return aRandom & n;
            auto lim = n;
            lim |= (lim >> 1);
            lim |= (lim >> 2);
            lim |= (lim >> 4);
            lim |= (lim >> 8);
            lim |= (lim >> 16);
            lim |= (lim >> 32);
            while ((aRandom &= lim) > n)
                aRandom = next_random();
            return aRandom;
        }

        void math_random(native_call& c)
        {
            auto const r = next_random();
            std::int64_t low;
            std::int64_t high;
            switch (c.count())
            {
            case 0u:
                c.ret(make_float(static_cast<double>(r >> 11) * (0.5 / static_cast<double>(std::uint64_t{ 1 } << 52))));
                return;
            case 1u:
                low = 1;
                high = check_integer(c, 0u);
                if (high == 0)
                {
                    // (math.random(0): an integer with all its bits random)
                    c.ret(make_integer(static_cast<std::int64_t>(r)));
                    return;
                }
                break;
            case 2u:
                low = check_integer(c, 0u);
                high = check_integer(c, 1u);
                break;
            default:
                runtime_error("wrong number of arguments");
            }
            if (low > high)
                argument_error(c, c.count() == 1u ? 0u : 1u, "interval is empty");
            auto const result = project(r, static_cast<std::uint64_t>(high) - static_cast<std::uint64_t>(low)) + static_cast<std::uint64_t>(low);
            c.ret(make_integer(static_cast<std::int64_t>(result)));
        }

        void math_randomseed(native_call& c)
        {
            std::uint64_t a;
            std::uint64_t b;
            if (c.none(0u))
            {
                a = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
                b = reinterpret_cast<std::uintptr_t>(&c);
            }
            else
            {
                a = static_cast<std::uint64_t>(check_integer(c, 0u));
                b = static_cast<std::uint64_t>(optional_integer(c, 1u, 0));
            }
            seed_random(a, b);
            c.ret(make_integer(static_cast<std::int64_t>(a)));
            c.ret(make_integer(static_cast<std::int64_t>(b)));
        }
    }

    namespace
    {
        // --- fast versions (see fast_native)

        value fast_floor(value const* aArguments, std::size_t aCount, value&)
        {
            if (aCount != 1u)
                return Empty;
            auto const n = aArguments[0];
            if (is_inline_integer(n))
                return n;
            if (is_float(n))
                return float_or_integer(std::floor(float_of(n)));
            return Empty;
        }

        value fast_abs(value const* aArguments, std::size_t aCount, value&)
        {
            if (aCount != 1u)
                return Empty;
            auto const n = aArguments[0];
            if (is_inline_integer(n))
                return make_integer(std::abs(inline_integer(n)));
            if (is_float(n))
                return make_float(std::fabs(float_of(n)));
            return Empty;
        }

        value fast_sqrt(value const* aArguments, std::size_t aCount, value&)
        {
            if (aCount != 1u)
                return Empty;
            auto const n = aArguments[0];
            if (is_inline_integer(n))
                return make_float(std::sqrt(static_cast<double>(inline_integer(n))));
            if (is_float(n))
                return make_float(std::sqrt(float_of(n)));
            return Empty;
        }

        value fast_min_max(value const* aArguments, std::size_t aCount, bool aMax)
        {
            if (aCount != 2u)
                return Empty;
            auto const a = aArguments[0];
            auto const b = aArguments[1];
            if (is_inline_integer(a) && is_inline_integer(b))
                return (aMax ? inline_integer(b) > inline_integer(a) : inline_integer(b) < inline_integer(a)) ? b : a;
            if (is_float(a) && is_float(b) && !std::isnan(float_of(a)) && !std::isnan(float_of(b)))
                return (aMax ? float_of(b) > float_of(a) : float_of(b) < float_of(a)) ? b : a;
            return Empty;
        }

        value fast_max(value const* aArguments, std::size_t aCount, value&) { return fast_min_max(aArguments, aCount, true); }
        value fast_min(value const* aArguments, std::size_t aCount, value&) { return fast_min_max(aArguments, aCount, false); }
    }

    void open_math()
    {
        auto const t = library_table("math");
        value_list keep{ t };
        register_library(t, "math", {
            { "abs", &math_abs },
            { "ceil", &math_ceil },
            { "floor", &math_floor },
            { "fmod", &math_fmod },
            { "modf", &math_modf },
            { "sqrt", &math_sqrt },
            { "sin", &math_sin },
            { "cos", &math_cos },
            { "tan", &math_tan },
            { "asin", &math_asin },
            { "acos", &math_acos },
            { "atan", &math_atan },
            { "exp", &math_exp },
            { "log", &math_log },
            { "deg", &math_deg },
            { "rad", &math_rad },
            { "tointeger", &math_tointeger },
            { "type", &math_type },
            { "ult", &math_ult },
            { "max", &math_max },
            { "min", &math_min },
            { "random", &math_random },
            { "randomseed", &math_randomseed } });
        set_fast(t, "floor", &fast_floor);
        set_fast(t, "abs", &fast_abs);
        set_fast(t, "sqrt", &fast_sqrt);
        set_fast(t, "max", &fast_max);
        set_fast(t, "min", &fast_min);
        raw_set(t, intern("pi"), make_float(std::numbers::pi));
        raw_set(t, intern("huge"), make_float(std::numeric_limits<double>::infinity()));
        raw_set(t, intern("maxinteger"), make_integer(std::numeric_limits<std::int64_t>::max()));
        raw_set(t, intern("mininteger"), make_integer(std::numeric_limits<std::int64_t>::min()));
        seed_random(static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()), reinterpret_cast<std::uintptr_t>(&t));
    }
}
