/*
  stdbit.h

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

/* <stdbit.h> (7.18): bit and byte utilities */

#ifndef __NEOS_STDBIT_H
#define __NEOS_STDBIT_H
#define __STDC_VERSION_STDBIT_H__ 202311L

#include <stddef.h>
#include <stdint.h>

#define __STDC_ENDIAN_LITTLE__ 1234
#define __STDC_ENDIAN_BIG__ 4321
#define __STDC_ENDIAN_NATIVE__ __STDC_ENDIAN_LITTLE__

static inline unsigned int stdc_leading_zeros_uc(unsigned char x) { return x == 0 ? 8u : (unsigned int)(__builtin_clzll((unsigned long long)x) - 56); }
static inline unsigned int stdc_leading_ones_uc(unsigned char x) { return stdc_leading_zeros_uc((unsigned char)~x); }
static inline unsigned int stdc_trailing_zeros_uc(unsigned char x) { return x == 0 ? 8u : (unsigned int)__builtin_ctzll((unsigned long long)x); }
static inline unsigned int stdc_trailing_ones_uc(unsigned char x) { return stdc_trailing_zeros_uc((unsigned char)~x); }
static inline unsigned int stdc_first_leading_zero_uc(unsigned char x) { return x == (unsigned char)~(unsigned char)0 ? 0u : stdc_leading_ones_uc(x) + 1u; }
static inline unsigned int stdc_first_leading_one_uc(unsigned char x) { return x == 0 ? 0u : stdc_leading_zeros_uc(x) + 1u; }
static inline unsigned int stdc_first_trailing_zero_uc(unsigned char x) { return x == (unsigned char)~(unsigned char)0 ? 0u : stdc_trailing_ones_uc(x) + 1u; }
static inline unsigned int stdc_first_trailing_one_uc(unsigned char x) { return x == 0 ? 0u : stdc_trailing_zeros_uc(x) + 1u; }
static inline unsigned int stdc_count_ones_uc(unsigned char x) { return (unsigned int)__builtin_popcountll((unsigned long long)x); }
static inline unsigned int stdc_count_zeros_uc(unsigned char x) { return 8u - stdc_count_ones_uc(x); }
static inline bool stdc_has_single_bit_uc(unsigned char x) { return stdc_count_ones_uc(x) == 1u; }
static inline unsigned int stdc_bit_width_uc(unsigned char x) { return 8u - stdc_leading_zeros_uc(x); }
static inline unsigned char stdc_bit_floor_uc(unsigned char x) { return x == 0 ? (unsigned char)0 : (unsigned char)((unsigned char)1 << (stdc_bit_width_uc(x) - 1u)); }
static inline unsigned char stdc_bit_ceil_uc(unsigned char x) { return x <= 1 ? (unsigned char)1 : stdc_bit_width_uc((unsigned char)(x - 1)) >= 8u ? (unsigned char)0 : (unsigned char)((unsigned char)1 << stdc_bit_width_uc((unsigned char)(x - 1))); }

static inline unsigned int stdc_leading_zeros_us(unsigned short x) { return x == 0 ? 16u : (unsigned int)(__builtin_clzll((unsigned long long)x) - 48); }
static inline unsigned int stdc_leading_ones_us(unsigned short x) { return stdc_leading_zeros_us((unsigned short)~x); }
static inline unsigned int stdc_trailing_zeros_us(unsigned short x) { return x == 0 ? 16u : (unsigned int)__builtin_ctzll((unsigned long long)x); }
static inline unsigned int stdc_trailing_ones_us(unsigned short x) { return stdc_trailing_zeros_us((unsigned short)~x); }
static inline unsigned int stdc_first_leading_zero_us(unsigned short x) { return x == (unsigned short)~(unsigned short)0 ? 0u : stdc_leading_ones_us(x) + 1u; }
static inline unsigned int stdc_first_leading_one_us(unsigned short x) { return x == 0 ? 0u : stdc_leading_zeros_us(x) + 1u; }
static inline unsigned int stdc_first_trailing_zero_us(unsigned short x) { return x == (unsigned short)~(unsigned short)0 ? 0u : stdc_trailing_ones_us(x) + 1u; }
static inline unsigned int stdc_first_trailing_one_us(unsigned short x) { return x == 0 ? 0u : stdc_trailing_zeros_us(x) + 1u; }
static inline unsigned int stdc_count_ones_us(unsigned short x) { return (unsigned int)__builtin_popcountll((unsigned long long)x); }
static inline unsigned int stdc_count_zeros_us(unsigned short x) { return 16u - stdc_count_ones_us(x); }
static inline bool stdc_has_single_bit_us(unsigned short x) { return stdc_count_ones_us(x) == 1u; }
static inline unsigned int stdc_bit_width_us(unsigned short x) { return 16u - stdc_leading_zeros_us(x); }
static inline unsigned short stdc_bit_floor_us(unsigned short x) { return x == 0 ? (unsigned short)0 : (unsigned short)((unsigned short)1 << (stdc_bit_width_us(x) - 1u)); }
static inline unsigned short stdc_bit_ceil_us(unsigned short x) { return x <= 1 ? (unsigned short)1 : stdc_bit_width_us((unsigned short)(x - 1)) >= 16u ? (unsigned short)0 : (unsigned short)((unsigned short)1 << stdc_bit_width_us((unsigned short)(x - 1))); }

static inline unsigned int stdc_leading_zeros_ui(unsigned int x) { return x == 0 ? 32u : (unsigned int)(__builtin_clzll((unsigned long long)x) - 32); }
static inline unsigned int stdc_leading_ones_ui(unsigned int x) { return stdc_leading_zeros_ui((unsigned int)~x); }
static inline unsigned int stdc_trailing_zeros_ui(unsigned int x) { return x == 0 ? 32u : (unsigned int)__builtin_ctzll((unsigned long long)x); }
static inline unsigned int stdc_trailing_ones_ui(unsigned int x) { return stdc_trailing_zeros_ui((unsigned int)~x); }
static inline unsigned int stdc_first_leading_zero_ui(unsigned int x) { return x == (unsigned int)~(unsigned int)0 ? 0u : stdc_leading_ones_ui(x) + 1u; }
static inline unsigned int stdc_first_leading_one_ui(unsigned int x) { return x == 0 ? 0u : stdc_leading_zeros_ui(x) + 1u; }
static inline unsigned int stdc_first_trailing_zero_ui(unsigned int x) { return x == (unsigned int)~(unsigned int)0 ? 0u : stdc_trailing_ones_ui(x) + 1u; }
static inline unsigned int stdc_first_trailing_one_ui(unsigned int x) { return x == 0 ? 0u : stdc_trailing_zeros_ui(x) + 1u; }
static inline unsigned int stdc_count_ones_ui(unsigned int x) { return (unsigned int)__builtin_popcountll((unsigned long long)x); }
static inline unsigned int stdc_count_zeros_ui(unsigned int x) { return 32u - stdc_count_ones_ui(x); }
static inline bool stdc_has_single_bit_ui(unsigned int x) { return stdc_count_ones_ui(x) == 1u; }
static inline unsigned int stdc_bit_width_ui(unsigned int x) { return 32u - stdc_leading_zeros_ui(x); }
static inline unsigned int stdc_bit_floor_ui(unsigned int x) { return x == 0 ? (unsigned int)0 : (unsigned int)((unsigned int)1 << (stdc_bit_width_ui(x) - 1u)); }
static inline unsigned int stdc_bit_ceil_ui(unsigned int x) { return x <= 1 ? (unsigned int)1 : stdc_bit_width_ui((unsigned int)(x - 1)) >= 32u ? (unsigned int)0 : (unsigned int)((unsigned int)1 << stdc_bit_width_ui((unsigned int)(x - 1))); }

static inline unsigned int stdc_leading_zeros_ul(unsigned long x) { return x == 0 ? 64u : (unsigned int)__builtin_clzll(x); }
static inline unsigned int stdc_leading_ones_ul(unsigned long x) { return stdc_leading_zeros_ul((unsigned long)~x); }
static inline unsigned int stdc_trailing_zeros_ul(unsigned long x) { return x == 0 ? 64u : (unsigned int)__builtin_ctzll((unsigned long long)x); }
static inline unsigned int stdc_trailing_ones_ul(unsigned long x) { return stdc_trailing_zeros_ul((unsigned long)~x); }
static inline unsigned int stdc_first_leading_zero_ul(unsigned long x) { return x == (unsigned long)~(unsigned long)0 ? 0u : stdc_leading_ones_ul(x) + 1u; }
static inline unsigned int stdc_first_leading_one_ul(unsigned long x) { return x == 0 ? 0u : stdc_leading_zeros_ul(x) + 1u; }
static inline unsigned int stdc_first_trailing_zero_ul(unsigned long x) { return x == (unsigned long)~(unsigned long)0 ? 0u : stdc_trailing_ones_ul(x) + 1u; }
static inline unsigned int stdc_first_trailing_one_ul(unsigned long x) { return x == 0 ? 0u : stdc_trailing_zeros_ul(x) + 1u; }
static inline unsigned int stdc_count_ones_ul(unsigned long x) { return (unsigned int)__builtin_popcountll((unsigned long long)x); }
static inline unsigned int stdc_count_zeros_ul(unsigned long x) { return 64u - stdc_count_ones_ul(x); }
static inline bool stdc_has_single_bit_ul(unsigned long x) { return stdc_count_ones_ul(x) == 1u; }
static inline unsigned int stdc_bit_width_ul(unsigned long x) { return 64u - stdc_leading_zeros_ul(x); }
static inline unsigned long stdc_bit_floor_ul(unsigned long x) { return x == 0 ? (unsigned long)0 : (unsigned long)((unsigned long)1 << (stdc_bit_width_ul(x) - 1u)); }
static inline unsigned long stdc_bit_ceil_ul(unsigned long x) { return x <= 1 ? (unsigned long)1 : stdc_bit_width_ul((unsigned long)(x - 1)) >= 64u ? (unsigned long)0 : (unsigned long)((unsigned long)1 << stdc_bit_width_ul((unsigned long)(x - 1))); }

static inline unsigned int stdc_leading_zeros_ull(unsigned long long x) { return x == 0 ? 64u : (unsigned int)__builtin_clzll(x); }
static inline unsigned int stdc_leading_ones_ull(unsigned long long x) { return stdc_leading_zeros_ull((unsigned long long)~x); }
static inline unsigned int stdc_trailing_zeros_ull(unsigned long long x) { return x == 0 ? 64u : (unsigned int)__builtin_ctzll((unsigned long long)x); }
static inline unsigned int stdc_trailing_ones_ull(unsigned long long x) { return stdc_trailing_zeros_ull((unsigned long long)~x); }
static inline unsigned int stdc_first_leading_zero_ull(unsigned long long x) { return x == (unsigned long long)~(unsigned long long)0 ? 0u : stdc_leading_ones_ull(x) + 1u; }
static inline unsigned int stdc_first_leading_one_ull(unsigned long long x) { return x == 0 ? 0u : stdc_leading_zeros_ull(x) + 1u; }
static inline unsigned int stdc_first_trailing_zero_ull(unsigned long long x) { return x == (unsigned long long)~(unsigned long long)0 ? 0u : stdc_trailing_ones_ull(x) + 1u; }
static inline unsigned int stdc_first_trailing_one_ull(unsigned long long x) { return x == 0 ? 0u : stdc_trailing_zeros_ull(x) + 1u; }
static inline unsigned int stdc_count_ones_ull(unsigned long long x) { return (unsigned int)__builtin_popcountll((unsigned long long)x); }
static inline unsigned int stdc_count_zeros_ull(unsigned long long x) { return 64u - stdc_count_ones_ull(x); }
static inline bool stdc_has_single_bit_ull(unsigned long long x) { return stdc_count_ones_ull(x) == 1u; }
static inline unsigned int stdc_bit_width_ull(unsigned long long x) { return 64u - stdc_leading_zeros_ull(x); }
static inline unsigned long long stdc_bit_floor_ull(unsigned long long x) { return x == 0 ? (unsigned long long)0 : (unsigned long long)((unsigned long long)1 << (stdc_bit_width_ull(x) - 1u)); }
static inline unsigned long long stdc_bit_ceil_ull(unsigned long long x) { return x <= 1 ? (unsigned long long)1 : stdc_bit_width_ull((unsigned long long)(x - 1)) >= 64u ? (unsigned long long)0 : (unsigned long long)((unsigned long long)1 << stdc_bit_width_ull((unsigned long long)(x - 1))); }

#define stdc_leading_zeros(x) _Generic((x), unsigned char: stdc_leading_zeros_uc, unsigned short: stdc_leading_zeros_us, unsigned int: stdc_leading_zeros_ui, unsigned long: stdc_leading_zeros_ul, unsigned long long: stdc_leading_zeros_ull)(x)
#define stdc_leading_ones(x) _Generic((x), unsigned char: stdc_leading_ones_uc, unsigned short: stdc_leading_ones_us, unsigned int: stdc_leading_ones_ui, unsigned long: stdc_leading_ones_ul, unsigned long long: stdc_leading_ones_ull)(x)
#define stdc_trailing_zeros(x) _Generic((x), unsigned char: stdc_trailing_zeros_uc, unsigned short: stdc_trailing_zeros_us, unsigned int: stdc_trailing_zeros_ui, unsigned long: stdc_trailing_zeros_ul, unsigned long long: stdc_trailing_zeros_ull)(x)
#define stdc_trailing_ones(x) _Generic((x), unsigned char: stdc_trailing_ones_uc, unsigned short: stdc_trailing_ones_us, unsigned int: stdc_trailing_ones_ui, unsigned long: stdc_trailing_ones_ul, unsigned long long: stdc_trailing_ones_ull)(x)
#define stdc_first_leading_zero(x) _Generic((x), unsigned char: stdc_first_leading_zero_uc, unsigned short: stdc_first_leading_zero_us, unsigned int: stdc_first_leading_zero_ui, unsigned long: stdc_first_leading_zero_ul, unsigned long long: stdc_first_leading_zero_ull)(x)
#define stdc_first_leading_one(x) _Generic((x), unsigned char: stdc_first_leading_one_uc, unsigned short: stdc_first_leading_one_us, unsigned int: stdc_first_leading_one_ui, unsigned long: stdc_first_leading_one_ul, unsigned long long: stdc_first_leading_one_ull)(x)
#define stdc_first_trailing_zero(x) _Generic((x), unsigned char: stdc_first_trailing_zero_uc, unsigned short: stdc_first_trailing_zero_us, unsigned int: stdc_first_trailing_zero_ui, unsigned long: stdc_first_trailing_zero_ul, unsigned long long: stdc_first_trailing_zero_ull)(x)
#define stdc_first_trailing_one(x) _Generic((x), unsigned char: stdc_first_trailing_one_uc, unsigned short: stdc_first_trailing_one_us, unsigned int: stdc_first_trailing_one_ui, unsigned long: stdc_first_trailing_one_ul, unsigned long long: stdc_first_trailing_one_ull)(x)
#define stdc_count_ones(x) _Generic((x), unsigned char: stdc_count_ones_uc, unsigned short: stdc_count_ones_us, unsigned int: stdc_count_ones_ui, unsigned long: stdc_count_ones_ul, unsigned long long: stdc_count_ones_ull)(x)
#define stdc_count_zeros(x) _Generic((x), unsigned char: stdc_count_zeros_uc, unsigned short: stdc_count_zeros_us, unsigned int: stdc_count_zeros_ui, unsigned long: stdc_count_zeros_ul, unsigned long long: stdc_count_zeros_ull)(x)
#define stdc_has_single_bit(x) _Generic((x), unsigned char: stdc_has_single_bit_uc, unsigned short: stdc_has_single_bit_us, unsigned int: stdc_has_single_bit_ui, unsigned long: stdc_has_single_bit_ul, unsigned long long: stdc_has_single_bit_ull)(x)
#define stdc_bit_width(x) _Generic((x), unsigned char: stdc_bit_width_uc, unsigned short: stdc_bit_width_us, unsigned int: stdc_bit_width_ui, unsigned long: stdc_bit_width_ul, unsigned long long: stdc_bit_width_ull)(x)
#define stdc_bit_floor(x) _Generic((x), unsigned char: stdc_bit_floor_uc, unsigned short: stdc_bit_floor_us, unsigned int: stdc_bit_floor_ui, unsigned long: stdc_bit_floor_ul, unsigned long long: stdc_bit_floor_ull)(x)
#define stdc_bit_ceil(x) _Generic((x), unsigned char: stdc_bit_ceil_uc, unsigned short: stdc_bit_ceil_us, unsigned int: stdc_bit_ceil_ui, unsigned long: stdc_bit_ceil_ul, unsigned long long: stdc_bit_ceil_ull)(x)

#endif
