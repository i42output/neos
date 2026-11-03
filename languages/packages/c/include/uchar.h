/*
  uchar.h

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

/* <uchar.h> (7.30): Unicode utilities */

#ifndef __NEOS_UCHAR_H
#define __NEOS_UCHAR_H
#define __STDC_VERSION_UCHAR_H__ 202311L

#include <stddef.h>
#include <stdint.h>

typedef unsigned char char8_t;
typedef uint_least16_t char16_t;
typedef uint_least32_t char32_t;
#ifndef __NEOS_MBSTATE_T
#define __NEOS_MBSTATE_T
typedef struct __neos_mbstate { int __count; unsigned char __bytes[4]; } mbstate_t;
#endif

size_t mbrtoc8(char8_t* restrict pc8, const char* restrict s, size_t n, mbstate_t* restrict ps);
size_t c8rtomb(char* restrict s, char8_t c8, mbstate_t* restrict ps);
size_t mbrtoc16(char16_t* restrict pc16, const char* restrict s, size_t n, mbstate_t* restrict ps);
size_t c16rtomb(char* restrict s, char16_t c16, mbstate_t* restrict ps);
size_t mbrtoc32(char32_t* restrict pc32, const char* restrict s, size_t n, mbstate_t* restrict ps);
size_t c32rtomb(char* restrict s, char32_t c32, mbstate_t* restrict ps);

#endif
