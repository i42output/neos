/*
  lib/time.c

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

/*
  The C library: POSIX's clocks and sleeping (a part of the library in C: compiled with a program that uses it (see the 
  "provides" line below))
*/

/* provides: clock_gettime clock_getres nanosleep sleep usleep sched_yield */

#include <errno.h>
#include <sched.h>
#include <time.h>
#include <unistd.h>

int __neos_thread_sleep(long long seconds, long long nanoseconds);
void __neos_thread_yield(void);

int clock_gettime(clockid_t clock_id, struct timespec* tp)
{
    if ((clock_id != CLOCK_REALTIME && clock_id != CLOCK_MONOTONIC) || timespec_get(tp, clock_id == CLOCK_MONOTONIC ? TIME_MONOTONIC : TIME_UTC) == 0)
    {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int clock_getres(clockid_t clock_id, struct timespec* res)
{
    if ((clock_id != CLOCK_REALTIME && clock_id != CLOCK_MONOTONIC) || timespec_getres(res, clock_id == CLOCK_MONOTONIC ? TIME_MONOTONIC : TIME_UTC) == 0)
    {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

/* (a sleep is not interrupted: the time remaining is 0) */
int nanosleep(const struct timespec* rqtp, struct timespec* rmtp)
{
    int const error = __neos_thread_sleep(rqtp->tv_sec, rqtp->tv_nsec);
    if (error != 0)
    {
        errno = error;
        return -1;
    }
    if (rmtp != NULL)
    {
        rmtp->tv_sec = 0;
        rmtp->tv_nsec = 0;
    }
    return 0;
}

unsigned sleep(unsigned seconds)
{
    __neos_thread_sleep(seconds, 0);
    return 0;
}

int usleep(useconds_t usec)
{
    __neos_thread_sleep(usec / 1000000u, (usec % 1000000u) * 1000ll);
    return 0;
}

int sched_yield(void)
{
    __neos_thread_yield();
    return 0;
}
