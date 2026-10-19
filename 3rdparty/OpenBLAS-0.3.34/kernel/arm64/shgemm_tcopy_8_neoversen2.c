/***************************************************************************
 * Copyright (c) 2026, The OpenBLAS Project
 * All rights reserved.
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in
 * the documentation and/or other materials provided with the
 * distribution.
 * 3. Neither the name of the OpenBLAS project nor the names of
 * its contributors may be used to endorse or promote products
 * derived from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE OPENBLAS PROJECT OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 * *****************************************************************************/

#include <arm_sve.h>

#include "common.h"

int CNAME(BLASLONG m, BLASLONG n, IFLOAT *a, BLASLONG lda, IFLOAT *b) {
    BLASLONG i, j;
    IFLOAT *aoffset, *aoffset1;
    IFLOAT *boffset, *boffset1;
    IFLOAT *boffset2, *boffset3, *boffset4;

    aoffset = a;
    boffset = b;

    boffset2 = b + m * (n & ~7);
    boffset3 = b + m * (n & ~3);
    boffset4 = b + m * (n & ~1);

    svbool_t pg8 = svwhilelt_b16(0, 8);
    svbool_t pg4 = svwhilelt_b16(0, 4);

    for (j = 0; j < m; j++) {
        aoffset1 = aoffset;
        boffset1 = boffset;

        aoffset += lda;
        boffset += 8;

        for (i = 0; i < (n >> 3); i++) {
            svfloat16_t v0 = svld1_f16(pg8, (float16_t *)aoffset1);
            svst1_f16(pg8, (float16_t *)boffset1, v0);

            aoffset1 += 8;
            boffset1 += 8 * m;
        }

        if (n & 4) {
            svfloat16_t v0 = svld1_f16(pg4, (float16_t *)aoffset1);
            svst1_f16(pg4, (float16_t *)boffset2, v0);

            aoffset1 += 4;
            boffset2 += 4;
        }

        if (n & 2) {
            boffset3[0] = aoffset1[0];
            boffset3[1] = aoffset1[1];
            aoffset1 += 2;
            boffset3 += 2;
        }

        if (n & 1) {
            boffset4[0] = aoffset1[0];
            aoffset1 += 1;
            boffset4 += 1;
        }
    }

    return 0;
}