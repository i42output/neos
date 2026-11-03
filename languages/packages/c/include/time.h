/*
  time.h

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

/* <time.h> (7.29): date and time */

#ifndef __NEOS_TIME_H
#define __NEOS_TIME_H
#define __STDC_VERSION_TIME_H__ 202311L

#include <stddef.h>

#define CLOCKS_PER_SEC ((clock_t)1000000)
#define TIME_UTC 1
#define TIME_MONOTONIC 2

typedef long clock_t;
typedef long long time_t;

struct timespec
{
    time_t tv_sec;
    long tv_nsec;
};

struct tm
{
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

clock_t clock(void);
double difftime(time_t time1, time_t time0);
time_t mktime(struct tm* timeptr);
time_t timegm(struct tm* timeptr);
time_t time(time_t* timer);
int timespec_get(struct timespec* ts, int base);
int timespec_getres(struct timespec* ts, int base);
[[deprecated]] char* asctime(const struct tm* timeptr);
[[deprecated]] char* ctime(const time_t* timer);
struct tm* gmtime(const time_t* timer);
struct tm* gmtime_r(const time_t* timer, struct tm* buf);
struct tm* localtime(const time_t* timer);
struct tm* localtime_r(const time_t* timer, struct tm* buf);
size_t strftime(char* restrict s, size_t maxsize, const char* restrict format, const struct tm* restrict timeptr);

#endif
