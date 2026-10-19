/***************************************************************************
Copyright (c) 2023, The OpenBLAS Project
All rights reserved.
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:
1. Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in
the documentation and/or other materials provided with the
distribution.
3. Neither the name of the OpenBLAS project nor the names of
its contributors may be used to endorse or promote products
derived from this software without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE OPENBLAS PROJECT OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*****************************************************************************/

#define ASSEMBLER

#include "common.h"

#define N      $r4
#define X      $r5
#define INCX   $r6
#define Y      $r7
#define INCY   $r8

#define I      $r17
#define TEMP   $r18

/* Don't change following FR unless you know the effects. */
#define s1     $f8
#define s2     $f9
#define a1     $f10
#define b1     $f11

    PROLOGUE

    vxor.v    $vr8,   $vr8,    $vr8
    vxor.v    $vr9,   $vr9,    $vr9
    slli.d    INCX,   INCX,    BASE_SHIFT
    li.d      TEMP,   SIZE
    slli.d    INCY,   INCY,    BASE_SHIFT
    bge       $r0,    N,       .L999
    bne       INCX,   TEMP,    .L20   /* inc_x=1 */
    bne       INCY,   TEMP,    .L20   /* inc_y=1 */

    /* ((inc_x == 1) && (inc_y == 1)) */
    srai.d    I,      N,       4
    bge       $r0,    I,       .L12   /* FLOAT: <16 */
.L11:
    /* FLOAT: 16~ */
    vld       $vr0,   X,       0
    vld       $vr1,   X,       16
    vld       $vr2,   X,       32
    vld       $vr3,   X,       48
    vld       $vr4,   Y,       0
    vld       $vr5,   Y,       16
    vld       $vr6,   Y,       32
    vld       $vr7,   Y,       48
    addi.d    I,      I,       -1
    addi.d    X,      X,       64
    addi.d    Y,      Y,       64

    vfmadd.s    $vr8,   $vr0,    $vr4,   $vr8
    vfmadd.s    $vr9,   $vr1,    $vr5,   $vr9
    vfmadd.s    $vr8,   $vr2,    $vr6,   $vr8
    vfmadd.s    $vr9,   $vr3,    $vr7,   $vr9

    bnez      I,      .L11

    vfadd.s   $vr8,   $vr8,    $vr9
    vextrins.w $vr1,  $vr8,    0x01
    vextrins.w $vr2,  $vr8,    0x02
    vextrins.w $vr3,  $vr8,    0x03
    fadd.s     $f8,   $f8,     $f1
    fadd.s     $f8,   $f8,     $f2
    fadd.s     $f8,   $f8,     $f3
.L12:
    andi      I,      N,       0xf
    bge       $r0,    I,       .L999
.L13:
    addi.d    I,      I,       -1
    fld.s     $f0,    X,       0
    fld.s     $f4,    Y,       0
    addi.d    X,      X,       4
    addi.d    Y,      Y,       4
    fmadd.s   $f8,   $f0,   $f4,  $f8
    bnez      I,      .L13
    b         .L999

    /* !((inc_x == 1) && (inc_y == 1)) */
.L20:
    move      I,      N
.L21:
    addi.d    I,      I,       -1

    fld.s     $f0,    X,    0
    fld.s     $f4,    Y,    0
    add.d     X,      X,    INCX
    add.d     Y,      Y,    INCY
    fmadd.s   $f8,    $f0,  $f4,   $f8

    bnez      I,      .L21
    b         .L999

.L999:
    fmov.s    $f0,    $f8
    jirl      $r0,    $r1,     0x0

    EPILOGUE
