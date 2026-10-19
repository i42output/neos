/*
  pytime.hpp

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

  Portions of this file are derived from CPython 3.13 (Python/pytime.c), Copyright (c) 2001-2024 Python Software Foundation;
  All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see runtime/python/LICENSE.PSF).
  Changes: translated from C to C++ for the neos Python runtime plugin and adapted to its object model (see
  runtime/python/NOTICE).
*/

#pragma once

// Timeouts as CPython's PyTime_t (nanoseconds; Python/pytime.c): their conversion from Python objects (seconds or milliseconds, with
// CPython's rounding and messages), to milliseconds and to timeval, and deadlines (on the monotonic clock)

#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include "objects.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        namespace pytime
        {
            using time_type = std::int64_t;     ///< (PyTime_t: nanoseconds)
            constexpr time_type kMin = std::numeric_limits<time_type>::min();
            constexpr time_type kMax = std::numeric_limits<time_type>::max();
            constexpr time_type kSecondToNs = 1000000000;
            constexpr time_type kMsToNs = 1000000;
            constexpr time_type kUsToNs = 1000;
            constexpr time_type kNsToMs = 1000000;
            constexpr time_type kSecondToUs = 1000000;

            enum class rounding { Floor, Ceiling, HalfEven, Up, Timeout = Up };

            // (t + k, clamped)
            inline time_type add(time_type t1, time_type t2)
            {
                if (t2 > 0 && t1 > kMax - t2)
                    return kMax;
                if (t2 < 0 && t1 < kMin - t2)
                    return kMin;
                return t1 + t2;
            }
            // (t * k (k >= 0): false on overflow (t clamped))
            inline bool mul(time_type& t, time_type k)
            {
                if (k != 0 && ((t < kMin / k) || (kMax / k < t)))
                {
                    t = t >= 0 ? kMax : kMin;
                    return false;
                }
                t *= k;
                return true;
            }
            inline double round(double x, rounding aRounding)
            {
                volatile double d = x;
                if (aRounding == rounding::HalfEven)
                {
                    double rounded = std::round(d);
                    if (std::fabs(d - rounded) == 0.5)
                        rounded = 2.0 * std::round(d / 2.0);
                    d = rounded;
                }
                else if (aRounding == rounding::Ceiling)
                    d = std::ceil(d);
                else if (aRounding == rounding::Floor)
                    d = std::floor(d);
                else
                    d = d >= 0.0 ? std::ceil(d) : std::floor(d);
                return d;
            }
            inline void overflow()
            {
                error("OverflowError", "timestamp out of range for C PyTime_t");
            }
            // (an object (float or int) of units (aUnitToNs nanoseconds) as nanoseconds: false if it raised)
            inline bool from_object(time_type& aResult, object* aObject, rounding aRounding, time_type aUnitToNs)
            {
                if (kind_of(aObject) == NEOS_PY_FLOAT)
                {
                    double value = 0.0;
                    if (!to_double(aObject, value))
                        return false;
                    if (std::isnan(value))
                    {
                        error("ValueError", "Invalid value NaN (not a number)");
                        return false;
                    }
                    volatile double d = value;
                    d *= static_cast<double>(aUnitToNs);
                    d = round(d, aRounding);
                    if (!(static_cast<double>(kMin) <= d && d < -static_cast<double>(kMin)))
                    {
                        overflow();
                        aResult = 0;
                        return false;
                    }
                    aResult = static_cast<time_type>(d);
                    return true;
                }
                long long seconds = 0;
                if (!to_index(aObject, seconds))
                {
                    ref const e{ take_exception() };
                    if (exception_is(e.get(), "OverflowError"))
                        overflow();
                    else
                        reraise_exception(e.get());
                    return false;
                }
                time_type ns = seconds;
                if (!mul(ns, aUnitToNs))
                {
                    overflow();
                    return false;
                }
                aResult = ns;
                return true;
            }
            inline bool from_seconds_object(time_type& aResult, object* aObject, rounding aRounding) { return from_object(aResult, aObject, aRounding, kSecondToNs); }
            inline bool from_milliseconds_object(time_type& aResult, object* aObject, rounding aRounding) { return from_object(aResult, aObject, aRounding, kMsToNs); }
            inline double as_seconds_double(time_type ns)
            {
                volatile double d;
                if (ns % kSecondToNs == 0)
                    d = static_cast<double>(ns / kSecondToNs);
                else
                {
                    d = static_cast<double>(ns);
                    d /= 1e9;
                }
                return d;
            }
            inline time_type divide_round_up(time_type t, time_type k)
            {
                if (t >= 0)
                {
                    time_type q = t / k;
                    if (t % k)
                        q += 1;
                    return q;
                }
                time_type q = t / k;
                if (t % k)
                    q -= 1;
                return q;
            }
            inline time_type divide(time_type t, time_type k, rounding aRounding)
            {
                if (aRounding == rounding::HalfEven)
                {
                    time_type x = t / k;
                    time_type const r = t % k;
                    time_type const absR = r < 0 ? -r : r;
                    if (absR > k / 2 || (absR == k / 2 && ((x < 0 ? -x : x) & 1)))
                        x += t >= 0 ? 1 : -1;
                    return x;
                }
                if (aRounding == rounding::Ceiling)
                    return t >= 0 ? divide_round_up(t, k) : t / k;
                if (aRounding == rounding::Floor)
                    return t >= 0 ? t / k : divide_round_up(t, k);
                return divide_round_up(t, k);
            }
            inline time_type as_milliseconds(time_type ns, rounding aRounding) { return divide(ns, kNsToMs, aRounding); }
            // (as seconds and microseconds (0 <= aMicroseconds < 1000000): false if the seconds were clamped)
            inline bool as_timeval(time_type ns, time_type& aSeconds, long& aMicroseconds, rounding aRounding)
            {
                time_type const us = divide(ns, kUsToNs, aRounding);
                time_type q = us / kSecondToUs;
                time_type r = us % kSecondToUs;
                if (r < 0)
                {
                    if (q == kMin)
                    {
                        aSeconds = kMin;
                        aMicroseconds = 0;
                        return false;
                    }
                    r += kSecondToUs;
                    q -= 1;
                }
                aSeconds = q;
                aMicroseconds = static_cast<long>(r);
                return true;
            }
            inline time_type monotonic()
            {
                return static_cast<time_type>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
            }
            inline time_type deadline_init(time_type aTimeout) { return add(monotonic(), aTimeout); }
            inline time_type deadline_get(time_type aDeadline) { return aDeadline - monotonic(); }
        }
    }
}
