/*
  lib_time.cpp

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

// The C language's runtime: <time.h> (7.29). time_t is a count of seconds (long long); clock_t counts microseconds
// (CLOCKS_PER_SEC 1000000); struct tm is its nine int members (as <time.h> declares it); struct timespec a long long and a long

#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>
#include "core.hpp"

namespace neos::c_runtime
{
    namespace
    {
        std::tm to_host(std::int64_t aTm)
        {
            auto const* t = at<std::int32_t>(static_cast<address>(aTm));
            std::tm r{};
            r.tm_sec = t[0];
            r.tm_min = t[1];
            r.tm_hour = t[2];
            r.tm_mday = t[3];
            r.tm_mon = t[4];
            r.tm_year = t[5];
            r.tm_wday = t[6];
            r.tm_yday = t[7];
            r.tm_isdst = t[8];
            return r;
        }

        void from_host(std::tm const& aTm, address aTo)
        {
            auto* t = at<std::int32_t>(aTo);
            t[0] = aTm.tm_sec;
            t[1] = aTm.tm_min;
            t[2] = aTm.tm_hour;
            t[3] = aTm.tm_mday;
            t[4] = aTm.tm_mon;
            t[5] = aTm.tm_year;
            t[6] = aTm.tm_wday;
            t[7] = aTm.tm_yday;
            t[8] = aTm.tm_isdst;
        }

        address static_tm()
        {
            static address sTm = 0u;
            if (sTm == 0u)
                sTm = allocate(40u);
            return sTm;
        }

        std::int64_t c_clock()
        {
            // (the processor time used: from the host's clock, in microseconds)
            auto const c = std::clock();
            if (c == static_cast<std::clock_t>(-1))
                return -1;
            return static_cast<std::int64_t>(static_cast<double>(c) * 1000000.0 / CLOCKS_PER_SEC);
        }

        double c_difftime(std::int64_t aEnd, std::int64_t aStart) { return static_cast<double>(aEnd - aStart); }

        std::int64_t c_mktime(std::int64_t aTm)
        {
            auto t = to_host(aTm);
            auto const r = std::mktime(&t);
            if (r != static_cast<std::time_t>(-1))
                from_host(t, static_cast<address>(aTm));
            return static_cast<std::int64_t>(r);
        }

        std::int64_t c_timegm(std::int64_t aTm)
        {
            auto t = to_host(aTm);
#if defined(_WIN32)
            auto const r = _mkgmtime(&t);
#else
            auto const r = ::timegm(&t);
#endif
            if (r != static_cast<std::time_t>(-1))
                from_host(t, static_cast<address>(aTm));
            return static_cast<std::int64_t>(r);
        }

        std::int64_t c_time(std::int64_t aTimer)
        {
            auto const now = static_cast<std::int64_t>(std::time(nullptr));
            if (aTimer != 0)
                *at<std::int64_t>(static_cast<address>(aTimer)) = now;
            return now;
        }

        std::int32_t c_timespec_get(std::int64_t aTs, std::int32_t aBase)
        {
            // (TIME_UTC 1, TIME_MONOTONIC 2)
            std::int64_t ns = 0;
            if (aBase == 1)
                ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            else if (aBase == 2)
                ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            else
                return 0;
            auto* ts = at<std::int64_t>(static_cast<address>(aTs));
            ts[0] = ns / 1000000000;
            ts[1] = ns % 1000000000;
            return aBase;
        }

        std::int32_t c_timespec_getres(std::int64_t aTs, std::int32_t aBase)
        {
            if (aBase != 1 && aBase != 2)
                return 0;
            if (aTs != 0)
            {
                auto* ts = at<std::int64_t>(static_cast<address>(aTs));
                ts[0] = 0;
                ts[1] = 1;
            }
            return aBase;
        }

        std::int64_t text_result(std::string const& aText)
        {
            static address sBuffer = 0u;
            if (sBuffer == 0u)
                sBuffer = allocate(128u);
            auto const n = std::min<std::size_t>(aText.size(), 127u);
            std::memcpy(at<char>(sBuffer), aText.data(), n);
            at<char>(sBuffer)[n] = '\0';
            return static_cast<std::int64_t>(sBuffer);
        }

        std::string asctime_text(std::tm const& aTm)
        {
            static char const* const sDays[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
            static char const* const sMonths[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%.3s %.3s%3d %.2d:%.2d:%.2d %d\n", sDays[((aTm.tm_wday % 7) + 7) % 7], sMonths[((aTm.tm_mon % 12) + 12) % 12],
                aTm.tm_mday, aTm.tm_hour, aTm.tm_min, aTm.tm_sec, 1900 + aTm.tm_year);
            return buffer;
        }

        std::int64_t c_asctime(std::int64_t aTm) { return text_result(asctime_text(to_host(aTm))); }

        bool host_time(std::int64_t aTimer, bool aLocal, std::tm& aResult)
        {
            std::time_t const t = static_cast<std::time_t>(*at<std::int64_t>(static_cast<address>(aTimer)));
#if defined(_WIN32)
            return (aLocal ? localtime_s(&aResult, &t) : gmtime_s(&aResult, &t)) == 0;
#else
            return (aLocal ? localtime_r(&t, &aResult) : gmtime_r(&t, &aResult)) != nullptr;
#endif
        }

        std::int64_t c_ctime(std::int64_t aTimer)
        {
            std::tm t{};
            if (!host_time(aTimer, true, t))
                return 0;
            return text_result(asctime_text(t));
        }

        std::int64_t c_gmtime_r(std::int64_t aTimer, std::int64_t aResult)
        {
            std::tm t{};
            if (!host_time(aTimer, false, t))
                return 0;
            from_host(t, static_cast<address>(aResult));
            return aResult;
        }

        std::int64_t c_localtime_r(std::int64_t aTimer, std::int64_t aResult)
        {
            std::tm t{};
            if (!host_time(aTimer, true, t))
                return 0;
            from_host(t, static_cast<address>(aResult));
            return aResult;
        }

        std::int64_t c_gmtime(std::int64_t aTimer) { return c_gmtime_r(aTimer, static_cast<std::int64_t>(static_tm())); }
        std::int64_t c_localtime(std::int64_t aTimer) { return c_localtime_r(aTimer, static_cast<std::int64_t>(static_tm())); }

        std::int64_t c_strftime(std::int64_t aBuffer, std::int64_t aSize, std::int64_t aFormat, std::int64_t aTm)
        {
            auto const t = to_host(aTm);
            return static_cast<std::int64_t>(std::strftime(at<char>(static_cast<address>(aBuffer)), static_cast<std::size_t>(aSize),
                str(static_cast<address>(aFormat)), &t));
        }

        function_table const sTimeFunctions
        {
            library("clock", NEOS_RT_FUNCTION(c_clock)), library("difftime", NEOS_RT_FUNCTION(c_difftime)),
            library("mktime", NEOS_RT_FUNCTION(c_mktime)), library("time", NEOS_RT_FUNCTION(c_time)),
            library("timespec_get", NEOS_RT_FUNCTION(c_timespec_get)), library("timespec_getres", NEOS_RT_FUNCTION(c_timespec_getres)),
            library("asctime", NEOS_RT_FUNCTION(c_asctime)), library("ctime", NEOS_RT_FUNCTION(c_ctime)),
            library("gmtime", NEOS_RT_FUNCTION(c_gmtime)), library("gmtime_r", NEOS_RT_FUNCTION(c_gmtime_r)),
            library("localtime", NEOS_RT_FUNCTION(c_localtime)), library("localtime_r", NEOS_RT_FUNCTION(c_localtime_r)),
            library("strftime", NEOS_RT_FUNCTION(c_strftime)), library("timegm", NEOS_RT_FUNCTION(c_timegm))
        };
    }
}
