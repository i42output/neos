/*
  float.h

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

/* <float.h> (5.2.5.3.3): characteristics of floating types */

#ifndef __NEOS_FLOAT_H
#define __NEOS_FLOAT_H
#define __STDC_VERSION_FLOAT_H__ 202311L

/* (float and double are IEC 60559's binary32 and binary64; long double is double) */
#define FLT_EVAL_METHOD 0
#define FLT_ROUNDS 1
#define FLT_RADIX 2
#define DECIMAL_DIG 17

#define FLT_HAS_SUBNORM 1
#define FLT_MANT_DIG 24
#define FLT_DECIMAL_DIG 9
#define FLT_DIG 6
#define FLT_MIN_EXP (-125)
#define FLT_MIN_10_EXP (-37)
#define FLT_MAX_EXP 128
#define FLT_MAX_10_EXP 38
#define FLT_MAX 0x1.fffffep+127F
#define FLT_NORM_MAX 0x1.fffffep+127F
#define FLT_EPSILON 0x1p-23F
#define FLT_MIN 0x1p-126F
#define FLT_TRUE_MIN 0x1p-149F

#define DBL_HAS_SUBNORM 1
#define DBL_MANT_DIG 53
#define DBL_DECIMAL_DIG 17
#define DBL_DIG 15
#define DBL_MIN_EXP (-1021)
#define DBL_MIN_10_EXP (-307)
#define DBL_MAX_EXP 1024
#define DBL_MAX_10_EXP 308
#define DBL_MAX 0x1.fffffffffffffp+1023
#define DBL_NORM_MAX 0x1.fffffffffffffp+1023
#define DBL_EPSILON 0x1p-52
#define DBL_MIN 0x1p-1022
#define DBL_TRUE_MIN 0x1p-1074

#define LDBL_HAS_SUBNORM 1
#define LDBL_MANT_DIG 53
#define LDBL_DECIMAL_DIG 17
#define LDBL_DIG 15
#define LDBL_MIN_EXP (-1021)
#define LDBL_MIN_10_EXP (-307)
#define LDBL_MAX_EXP 1024
#define LDBL_MAX_10_EXP 308
#define LDBL_MAX 0x1.fffffffffffffp+1023L
#define LDBL_NORM_MAX 0x1.fffffffffffffp+1023L
#define LDBL_EPSILON 0x1p-52L
#define LDBL_MIN 0x1p-1022L
#define LDBL_TRUE_MIN 0x1p-1074L

#define INFINITY (1.0F / 0.0F)
#define NAN (0.0F / 0.0F)

#endif
