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

#include <arm_neon.h>

#include "common.h"

static inline void transpose8x8(float16x8_t *rows, float16x8_t *cols) {
    float64x2_t b0 = vtrn1q_f64(vreinterpretq_f64_f16(rows[0]), vreinterpretq_f64_f16(rows[4]));
    float64x2_t b1 = vtrn1q_f64(vreinterpretq_f64_f16(rows[1]), vreinterpretq_f64_f16(rows[5]));
    float64x2_t b2 = vtrn1q_f64(vreinterpretq_f64_f16(rows[2]), vreinterpretq_f64_f16(rows[6]));
    float64x2_t b3 = vtrn1q_f64(vreinterpretq_f64_f16(rows[3]), vreinterpretq_f64_f16(rows[7]));
    float64x2_t b4 = vtrn2q_f64(vreinterpretq_f64_f16(rows[0]), vreinterpretq_f64_f16(rows[4]));
    float64x2_t b5 = vtrn2q_f64(vreinterpretq_f64_f16(rows[1]), vreinterpretq_f64_f16(rows[5]));
    float64x2_t b6 = vtrn2q_f64(vreinterpretq_f64_f16(rows[2]), vreinterpretq_f64_f16(rows[6]));
    float64x2_t b7 = vtrn2q_f64(vreinterpretq_f64_f16(rows[3]), vreinterpretq_f64_f16(rows[7]));

    float32x4_t c0 = vtrn1q_f32(vreinterpretq_f32_f64(b0), vreinterpretq_f32_f64(b2));
    float32x4_t c1 = vtrn1q_f32(vreinterpretq_f32_f64(b1), vreinterpretq_f32_f64(b3));
    float32x4_t c2 = vtrn2q_f32(vreinterpretq_f32_f64(b0), vreinterpretq_f32_f64(b2));
    float32x4_t c3 = vtrn2q_f32(vreinterpretq_f32_f64(b1), vreinterpretq_f32_f64(b3));
    float32x4_t c4 = vtrn1q_f32(vreinterpretq_f32_f64(b4), vreinterpretq_f32_f64(b6));
    float32x4_t c5 = vtrn1q_f32(vreinterpretq_f32_f64(b5), vreinterpretq_f32_f64(b7));
    float32x4_t c6 = vtrn2q_f32(vreinterpretq_f32_f64(b4), vreinterpretq_f32_f64(b6));
    float32x4_t c7 = vtrn2q_f32(vreinterpretq_f32_f64(b5), vreinterpretq_f32_f64(b7));

    float16x8_t d0 = vtrn1q_f16(vreinterpretq_f16_f32(c0), vreinterpretq_f16_f32(c1));
    float16x8_t d1 = vtrn2q_f16(vreinterpretq_f16_f32(c0), vreinterpretq_f16_f32(c1));
    float16x8_t d2 = vtrn1q_f16(vreinterpretq_f16_f32(c2), vreinterpretq_f16_f32(c3));
    float16x8_t d3 = vtrn2q_f16(vreinterpretq_f16_f32(c2), vreinterpretq_f16_f32(c3));
    float16x8_t d4 = vtrn1q_f16(vreinterpretq_f16_f32(c4), vreinterpretq_f16_f32(c5));
    float16x8_t d5 = vtrn2q_f16(vreinterpretq_f16_f32(c4), vreinterpretq_f16_f32(c5));
    float16x8_t d6 = vtrn1q_f16(vreinterpretq_f16_f32(c6), vreinterpretq_f16_f32(c7));
    float16x8_t d7 = vtrn2q_f16(vreinterpretq_f16_f32(c6), vreinterpretq_f16_f32(c7));

    cols[0] = d0;
    cols[1] = d1;
    cols[2] = d2;
    cols[3] = d3;
    cols[4] = d4;
    cols[5] = d5;
    cols[6] = d6;
    cols[7] = d7;
}

static inline void transpose_4x4(float16x4_t *rows, float16x4_t *cols) {
    float16x8_t t0 = vcombine_f16(rows[0], vdup_n_f16(0.0f));
    float16x8_t t1 = vcombine_f16(rows[1], vdup_n_f16(0.0f));
    float16x8_t t2 = vcombine_f16(rows[2], vdup_n_f16(0.0f));
    float16x8_t t3 = vcombine_f16(rows[3], vdup_n_f16(0.0f));

    float16x8_t t02 = vzip1q_f16(t0, t2);
    float16x8_t t13 = vzip1q_f16(t1, t3);

    float16x8x2_t t0123 = vzipq_f16(t02, t13);

    cols[0] = vget_low_f16(t0123.val[0]);
    cols[1] = vget_high_f16(t0123.val[0]);
    cols[2] = vget_low_f16(t0123.val[1]);
    cols[3] = vget_high_f16(t0123.val[1]);
}

int CNAME(BLASLONG m, BLASLONG n, IFLOAT *a, BLASLONG lda, IFLOAT *b) {
    BLASLONG i, j;
    IFLOAT *a_offset = a;
    IFLOAT *b_offset = b;

    float16x8_t v0, v1, v2, v3, v4, v5, v6, v7;
    float16x4_t v8, v9, v10, v11;

    BLASLONG n8 = n >> 3;

    for (j = 0; j < n8; j++) {
        IFLOAT *a0 = a_offset;
        IFLOAT *a1 = a0 + lda;
        IFLOAT *a2 = a1 + lda;
        IFLOAT *a3 = a2 + lda;
        IFLOAT *a4 = a3 + lda;
        IFLOAT *a5 = a4 + lda;
        IFLOAT *a6 = a5 + lda;
        IFLOAT *a7 = a6 + lda;
        a_offset += 8 * lda;

        BLASLONG m8 = m >> 3;
        for (i = 0; i < m8; i++) {
            v0 = vld1q_f16((float16_t *)a0);
            v1 = vld1q_f16((float16_t *)a1);
            v2 = vld1q_f16((float16_t *)a2);
            v3 = vld1q_f16((float16_t *)a3);
            v4 = vld1q_f16((float16_t *)a4);
            v5 = vld1q_f16((float16_t *)a5);
            v6 = vld1q_f16((float16_t *)a6);
            v7 = vld1q_f16((float16_t *)a7);

            float16x8_t rows[8] = {v0, v1, v2, v3, v4, v5, v6, v7};
            float16x8_t cols[8];
            transpose8x8(rows, cols);

            vst1q_f16((float16_t *)b_offset, cols[0]);
            vst1q_f16((float16_t *)b_offset + 8, cols[1]);
            vst1q_f16((float16_t *)b_offset + 16, cols[2]);
            vst1q_f16((float16_t *)b_offset + 24, cols[3]);
            vst1q_f16((float16_t *)b_offset + 32, cols[4]);
            vst1q_f16((float16_t *)b_offset + 40, cols[5]);
            vst1q_f16((float16_t *)b_offset + 48, cols[6]);
            vst1q_f16((float16_t *)b_offset + 56, cols[7]);

            a0 += 8;
            a1 += 8;
            a2 += 8;
            a3 += 8;
            a4 += 8;
            a5 += 8;
            a6 += 8;
            a7 += 8;
            b_offset += 64;
        }

        BLASLONG i = (m & 7);
        if (i > 0) {
            for (BLASLONG k = 0; k < i; k++) {
                *(b_offset + 0) = *a0;
                *(b_offset + 1) = *a1;
                *(b_offset + 2) = *a2;
                *(b_offset + 3) = *a3;
                *(b_offset + 4) = *a4;
                *(b_offset + 5) = *a5;
                *(b_offset + 6) = *a6;
                *(b_offset + 7) = *a7;

                a0++;
                a1++;
                a2++;
                a3++;
                a4++;
                a5++;
                a6++;
                a7++;

                b_offset += 8;
            }
        }
    }

    if (n & 4) {
        IFLOAT *a0 = a_offset;
        IFLOAT *a1 = a0 + lda;
        IFLOAT *a2 = a1 + lda;
        IFLOAT *a3 = a2 + lda;
        a_offset += 4 * lda;

        BLASLONG m4 = m >> 2;
        for (i = 0; i < m4; i++) {
            v8 = vld1_f16((float16_t *)a0);
            v9 = vld1_f16((float16_t *)a1);
            v10 = vld1_f16((float16_t *)a2);
            v11 = vld1_f16((float16_t *)a3);

            float16x4_t rows[4] = {v8, v9, v10, v11};
            float16x4_t cols[4];
            transpose_4x4(rows, cols);

            vst1_f16((float16_t *)b_offset, cols[0]);
            vst1_f16((float16_t *)b_offset + 4, cols[1]);
            vst1_f16((float16_t *)b_offset + 8, cols[2]);
            vst1_f16((float16_t *)b_offset + 12, cols[3]);

            a0 += 4;
            a1 += 4;
            a2 += 4;
            a3 += 4;
            b_offset += 16;
        }

        BLASLONG i = (m & 3);
        if (i > 0) {
            for (BLASLONG k = 0; k < i; k++) {
                *(b_offset + 0) = *a0;
                *(b_offset + 1) = *a1;
                *(b_offset + 2) = *a2;
                *(b_offset + 3) = *a3;

                a0++;
                a1++;
                a2++;
                a3++;

                b_offset += 4;
            }
        }
    }

    if (n & 2) {
        IFLOAT *a0 = a_offset;
        IFLOAT *a1 = a0 + lda;
        a_offset += 2 * lda;

        BLASLONG m2 = m >> 1;
        for (i = 0; i < m2; i++) {

            v8 = vld1_f16((float16_t *)a0);
            v9 = vld1_f16((float16_t *)a1);

            float16_t col0[2] = {vget_lane_f16(v8, 0), vget_lane_f16(v9, 0)};
            float16_t col1[2] = {vget_lane_f16(v8, 1), vget_lane_f16(v9, 1)};

            b_offset[0] = col0[0];
            b_offset[1] = col0[1];
            b_offset[2] = col1[0];
            b_offset[3] = col1[1];

            a0 += 2;
            a1 += 2;
            b_offset += 4;
        }

        if (m & 1) {
            b_offset[0] = *a0;
            b_offset[1] = *a1;
            b_offset += 2;
        }
    }

    if (n & 1) {
        IFLOAT *a0 = a_offset;
        for (i = 0; i < m; i++) {
            *b_offset++ = *a0;
            a0++;
        }
    }

    return 0;
}