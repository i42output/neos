/*
  lib_numerics.cpp

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

// The Ada runtime: the native primitives of the standard library (numerics): see ada.hpp
//
// Ada.Numerics.Generic_Elementary_Functions (and the complex packages, through it) import the C library's elementary
// functions on double (Long_Float); the Ada bodies check the Reference Manual's domain and pole rules (Argument_Error,
// Constraint_Error) and its prescribed results before calling them, so these never see an argument outside their domain.
// Ada.Numerics.Float_Random and Discrete_Random import random_seed for their time-dependent Reset.

#include <atomic>
#include <chrono>
#include <cmath>
#include <random>
#include "ada.hpp"

namespace neos::ada_runtime
{
}

namespace
{
    using namespace neos::ada_runtime;

    NEOS_ADA_PRIMITIVE double neos_ada_sqrt(double aX) { return std::sqrt(aX); }
    NEOS_ADA_PRIMITIVE double neos_ada_exp(double aX) { return std::exp(aX); }
    NEOS_ADA_PRIMITIVE double neos_ada_log(double aX) { return std::log(aX); }
    NEOS_ADA_PRIMITIVE double neos_ada_pow(double aX, double aY) { return std::pow(aX, aY); }
    NEOS_ADA_PRIMITIVE double neos_ada_sin(double aX) { return std::sin(aX); }
    NEOS_ADA_PRIMITIVE double neos_ada_cos(double aX) { return std::cos(aX); }
    NEOS_ADA_PRIMITIVE double neos_ada_tan(double aX) { return std::tan(aX); }
    NEOS_ADA_PRIMITIVE double neos_ada_asin(double aX) { return std::asin(aX); }
    NEOS_ADA_PRIMITIVE double neos_ada_acos(double aX) { return std::acos(aX); }
    NEOS_ADA_PRIMITIVE double neos_ada_atan2(double aY, double aX) { return std::atan2(aY, aX); }
    NEOS_ADA_PRIMITIVE double neos_ada_sinh(double aX) { return std::sinh(aX); }
    NEOS_ADA_PRIMITIVE double neos_ada_cosh(double aX) { return std::cosh(aX); }
    NEOS_ADA_PRIMITIVE double neos_ada_tanh(double aX) { return std::tanh(aX); }
    NEOS_ADA_PRIMITIVE double neos_ada_asinh(double aX) { return std::asinh(aX); }
    NEOS_ADA_PRIMITIVE double neos_ada_acosh(double aX) { return std::acosh(aX); }
    NEOS_ADA_PRIMITIVE double neos_ada_atanh(double aX) { return std::atanh(aX); }

    // a seed for a random number generator's time-dependent Reset: the system's entropy source mixed with the clock, so 
    // that calls made at the same time give different seeds
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_random_seed()
    {
        static std::atomic<std::uint64_t> sCounter{ 0u };
        std::uint64_t seed = static_cast<std::uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
        seed ^= (++sCounter) * 0x9E3779B97F4A7C15ull;
        try
        {
            std::random_device device;
            seed ^= (static_cast<std::uint64_t>(device()) << 32u) ^ static_cast<std::uint64_t>(device());
        }
        catch (...)
        {
        }
        return static_cast<std::int64_t>(seed);
    }

    [[maybe_unused]] constexpr auto I64 = NEOS_RT_I64;
    [[maybe_unused]] constexpr auto F64 = NEOS_RT_F64;

    function_table const snumericsFunctions{
        { "sqrt", NEOS_RT_FUNCTION(neos_ada_sqrt), F64, 1u, { F64 }, 0u },
        { "exp", NEOS_RT_FUNCTION(neos_ada_exp), F64, 1u, { F64 }, 0u },
        { "log", NEOS_RT_FUNCTION(neos_ada_log), F64, 1u, { F64 }, 0u },
        { "pow", NEOS_RT_FUNCTION(neos_ada_pow), F64, 2u, { F64, F64 }, 0u },
        { "sin", NEOS_RT_FUNCTION(neos_ada_sin), F64, 1u, { F64 }, 0u },
        { "cos", NEOS_RT_FUNCTION(neos_ada_cos), F64, 1u, { F64 }, 0u },
        { "tan", NEOS_RT_FUNCTION(neos_ada_tan), F64, 1u, { F64 }, 0u },
        { "asin", NEOS_RT_FUNCTION(neos_ada_asin), F64, 1u, { F64 }, 0u },
        { "acos", NEOS_RT_FUNCTION(neos_ada_acos), F64, 1u, { F64 }, 0u },
        { "atan2", NEOS_RT_FUNCTION(neos_ada_atan2), F64, 2u, { F64, F64 }, 0u },
        { "sinh", NEOS_RT_FUNCTION(neos_ada_sinh), F64, 1u, { F64 }, 0u },
        { "cosh", NEOS_RT_FUNCTION(neos_ada_cosh), F64, 1u, { F64 }, 0u },
        { "tanh", NEOS_RT_FUNCTION(neos_ada_tanh), F64, 1u, { F64 }, 0u },
        { "asinh", NEOS_RT_FUNCTION(neos_ada_asinh), F64, 1u, { F64 }, 0u },
        { "acosh", NEOS_RT_FUNCTION(neos_ada_acosh), F64, 1u, { F64 }, 0u },
        { "atanh", NEOS_RT_FUNCTION(neos_ada_atanh), F64, 1u, { F64 }, 0u },
        { "random_seed", NEOS_RT_FUNCTION(neos_ada_random_seed), I64, 0u, {}, 0u },
    };
}
