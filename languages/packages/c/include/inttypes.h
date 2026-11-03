/*
  inttypes.h

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

/* <inttypes.h> (7.8): format conversion of integer types */

#ifndef __NEOS_INTTYPES_H
#define __NEOS_INTTYPES_H
#define __STDC_VERSION_INTTYPES_H__ 202311L

#include <stdint.h>

typedef struct { intmax_t quot, rem; } imaxdiv_t;
#define PRId8 "hhd"
#define PRId16 "hd"
#define PRId32 "d"
#define PRId64 "ld"
#define PRIdLEAST8 "hhd"
#define PRIdLEAST16 "hd"
#define PRIdLEAST32 "d"
#define PRIdLEAST64 "ld"
#define PRIdFAST8 "hhd"
#define PRIdFAST16 "ld"
#define PRIdFAST32 "ld"
#define PRIdFAST64 "ld"
#define PRIdMAX "ld"
#define PRIdPTR "ld"
#define PRIi8 "hhi"
#define PRIi16 "hi"
#define PRIi32 "i"
#define PRIi64 "li"
#define PRIiLEAST8 "hhi"
#define PRIiLEAST16 "hi"
#define PRIiLEAST32 "i"
#define PRIiLEAST64 "li"
#define PRIiFAST8 "hhi"
#define PRIiFAST16 "li"
#define PRIiFAST32 "li"
#define PRIiFAST64 "li"
#define PRIiMAX "li"
#define PRIiPTR "li"
#define PRIo8 "hho"
#define PRIo16 "ho"
#define PRIo32 "o"
#define PRIo64 "lo"
#define PRIoLEAST8 "hho"
#define PRIoLEAST16 "ho"
#define PRIoLEAST32 "o"
#define PRIoLEAST64 "lo"
#define PRIoFAST8 "hho"
#define PRIoFAST16 "lo"
#define PRIoFAST32 "lo"
#define PRIoFAST64 "lo"
#define PRIoMAX "lo"
#define PRIoPTR "lo"
#define PRIu8 "hhu"
#define PRIu16 "hu"
#define PRIu32 "u"
#define PRIu64 "lu"
#define PRIuLEAST8 "hhu"
#define PRIuLEAST16 "hu"
#define PRIuLEAST32 "u"
#define PRIuLEAST64 "lu"
#define PRIuFAST8 "hhu"
#define PRIuFAST16 "lu"
#define PRIuFAST32 "lu"
#define PRIuFAST64 "lu"
#define PRIuMAX "lu"
#define PRIuPTR "lu"
#define PRIx8 "hhx"
#define PRIx16 "hx"
#define PRIx32 "x"
#define PRIx64 "lx"
#define PRIxLEAST8 "hhx"
#define PRIxLEAST16 "hx"
#define PRIxLEAST32 "x"
#define PRIxLEAST64 "lx"
#define PRIxFAST8 "hhx"
#define PRIxFAST16 "lx"
#define PRIxFAST32 "lx"
#define PRIxFAST64 "lx"
#define PRIxMAX "lx"
#define PRIxPTR "lx"
#define PRIX8 "hhX"
#define PRIX16 "hX"
#define PRIX32 "X"
#define PRIX64 "lX"
#define PRIXLEAST8 "hhX"
#define PRIXLEAST16 "hX"
#define PRIXLEAST32 "X"
#define PRIXLEAST64 "lX"
#define PRIXFAST8 "hhX"
#define PRIXFAST16 "lX"
#define PRIXFAST32 "lX"
#define PRIXFAST64 "lX"
#define PRIXMAX "lX"
#define PRIXPTR "lX"
#define PRIb8 "hhb"
#define PRIb16 "hb"
#define PRIb32 "b"
#define PRIb64 "lb"
#define PRIbLEAST8 "hhb"
#define PRIbLEAST16 "hb"
#define PRIbLEAST32 "b"
#define PRIbLEAST64 "lb"
#define PRIbFAST8 "hhb"
#define PRIbFAST16 "lb"
#define PRIbFAST32 "lb"
#define PRIbFAST64 "lb"
#define PRIbMAX "lb"
#define PRIbPTR "lb"
#define PRIB8 "hhB"
#define PRIB16 "hB"
#define PRIB32 "B"
#define PRIB64 "lB"
#define PRIBLEAST8 "hhB"
#define PRIBLEAST16 "hB"
#define PRIBLEAST32 "B"
#define PRIBLEAST64 "lB"
#define PRIBFAST8 "hhB"
#define PRIBFAST16 "lB"
#define PRIBFAST32 "lB"
#define PRIBFAST64 "lB"
#define PRIBMAX "lB"
#define PRIBPTR "lB"
#define SCNd8 "hhd"
#define SCNd16 "hd"
#define SCNd32 "d"
#define SCNd64 "ld"
#define SCNdLEAST8 "hhd"
#define SCNdLEAST16 "hd"
#define SCNdLEAST32 "d"
#define SCNdLEAST64 "ld"
#define SCNdFAST8 "hhd"
#define SCNdFAST16 "ld"
#define SCNdFAST32 "ld"
#define SCNdFAST64 "ld"
#define SCNdMAX "ld"
#define SCNdPTR "ld"
#define SCNi8 "hhi"
#define SCNi16 "hi"
#define SCNi32 "i"
#define SCNi64 "li"
#define SCNiLEAST8 "hhi"
#define SCNiLEAST16 "hi"
#define SCNiLEAST32 "i"
#define SCNiLEAST64 "li"
#define SCNiFAST8 "hhi"
#define SCNiFAST16 "li"
#define SCNiFAST32 "li"
#define SCNiFAST64 "li"
#define SCNiMAX "li"
#define SCNiPTR "li"
#define SCNo8 "hho"
#define SCNo16 "ho"
#define SCNo32 "o"
#define SCNo64 "lo"
#define SCNoLEAST8 "hho"
#define SCNoLEAST16 "ho"
#define SCNoLEAST32 "o"
#define SCNoLEAST64 "lo"
#define SCNoFAST8 "hho"
#define SCNoFAST16 "lo"
#define SCNoFAST32 "lo"
#define SCNoFAST64 "lo"
#define SCNoMAX "lo"
#define SCNoPTR "lo"
#define SCNu8 "hhu"
#define SCNu16 "hu"
#define SCNu32 "u"
#define SCNu64 "lu"
#define SCNuLEAST8 "hhu"
#define SCNuLEAST16 "hu"
#define SCNuLEAST32 "u"
#define SCNuLEAST64 "lu"
#define SCNuFAST8 "hhu"
#define SCNuFAST16 "lu"
#define SCNuFAST32 "lu"
#define SCNuFAST64 "lu"
#define SCNuMAX "lu"
#define SCNuPTR "lu"
#define SCNx8 "hhx"
#define SCNx16 "hx"
#define SCNx32 "x"
#define SCNx64 "lx"
#define SCNxLEAST8 "hhx"
#define SCNxLEAST16 "hx"
#define SCNxLEAST32 "x"
#define SCNxLEAST64 "lx"
#define SCNxFAST8 "hhx"
#define SCNxFAST16 "lx"
#define SCNxFAST32 "lx"
#define SCNxFAST64 "lx"
#define SCNxMAX "lx"
#define SCNxPTR "lx"
#define SCNX8 "hhX"
#define SCNX16 "hX"
#define SCNX32 "X"
#define SCNX64 "lX"
#define SCNXLEAST8 "hhX"
#define SCNXLEAST16 "hX"
#define SCNXLEAST32 "X"
#define SCNXLEAST64 "lX"
#define SCNXFAST8 "hhX"
#define SCNXFAST16 "lX"
#define SCNXFAST32 "lX"
#define SCNXFAST64 "lX"
#define SCNXMAX "lX"
#define SCNXPTR "lX"
#define SCNb8 "hhb"
#define SCNb16 "hb"
#define SCNb32 "b"
#define SCNb64 "lb"
#define SCNbLEAST8 "hhb"
#define SCNbLEAST16 "hb"
#define SCNbLEAST32 "b"
#define SCNbLEAST64 "lb"
#define SCNbFAST8 "hhb"
#define SCNbFAST16 "lb"
#define SCNbFAST32 "lb"
#define SCNbFAST64 "lb"
#define SCNbMAX "lb"
#define SCNbPTR "lb"

intmax_t imaxabs(intmax_t j);
imaxdiv_t imaxdiv(intmax_t numer, intmax_t denom);
intmax_t strtoimax(const char* restrict nptr, char** restrict endptr, int base);
/* (wchar_t is int: see <stddef.h>) */
intmax_t wcstoimax(const int* restrict nptr, int** restrict endptr, int base);
uintmax_t wcstoumax(const int* restrict nptr, int** restrict endptr, int base);
uintmax_t strtoumax(const char* restrict nptr, char** restrict endptr, int base);

#endif
