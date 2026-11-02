/*
  lib_annex.cpp

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

// The Ada runtime: the native primitives of the standard library (Annexes B to F: Interfaces.C, tasking control, execution
// time, ...): see ada.hpp
//
// Ada.Execution_Time imports cpu_time: the CPU time of the program's thread (which runs all of an Ada program's tasks), in
// nanoseconds.

#include <chrono>
#include <cstdint>
#include <ctime>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include "ada.hpp"

namespace
{
    using namespace neos::ada_runtime;

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_cpu_time()
    {
#ifdef _WIN32
        FILETIME creation, exit, kernel, user;
        if (!::GetThreadTimes(::GetCurrentThread(), &creation, &exit, &kernel, &user))
            return 0;
        auto const ticks = (static_cast<std::uint64_t>(kernel.dwHighDateTime) << 32u | kernel.dwLowDateTime) +
            (static_cast<std::uint64_t>(user.dwHighDateTime) << 32u | user.dwLowDateTime);
        return static_cast<std::int64_t>(ticks * 100u);
#elif defined(CLOCK_THREAD_CPUTIME_ID)
        timespec now{};
        if (::clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now) != 0)
            return 0;
        return static_cast<std::int64_t>(now.tv_sec) * 1000000000 + now.tv_nsec;
#else
        return static_cast<std::int64_t>(std::clock()) * (1000000000 / CLOCKS_PER_SEC);
#endif
    }

    [[maybe_unused]] constexpr auto I64 = NEOS_RT_I64;

    function_table const sannexFunctions{
        { "cpu_time", NEOS_RT_FUNCTION(neos_ada_cpu_time), I64, 0u, {}, 0u },
    };
}
