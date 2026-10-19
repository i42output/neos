/***************************************************************************
 * Copyright (c) 2026 The OpenBLAS Project
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

static inline void kernel_8x8(BLASLONG K, const float16_t *A, const float16_t *B, FLOAT *C, BLASLONG ldc, FLOAT alpha) {
    float32x4_t c0_low = vdupq_n_f32(0.0f);
    float32x4_t c0_high = vdupq_n_f32(0.0f);
    float32x4_t c1_low = vdupq_n_f32(0.0f);
    float32x4_t c1_high = vdupq_n_f32(0.0f);
    float32x4_t c2_low = vdupq_n_f32(0.0f);
    float32x4_t c2_high = vdupq_n_f32(0.0f);
    float32x4_t c3_low = vdupq_n_f32(0.0f);
    float32x4_t c3_high = vdupq_n_f32(0.0f);
    float32x4_t c4_low = vdupq_n_f32(0.0f);
    float32x4_t c4_high = vdupq_n_f32(0.0f);
    float32x4_t c5_low = vdupq_n_f32(0.0f);
    float32x4_t c5_high = vdupq_n_f32(0.0f);
    float32x4_t c6_low = vdupq_n_f32(0.0f);
    float32x4_t c6_high = vdupq_n_f32(0.0f);
    float32x4_t c7_low = vdupq_n_f32(0.0f);
    float32x4_t c7_high = vdupq_n_f32(0.0f);

    for (BLASLONG k = 0; k < K; ++k) {
        float16x8_t a_f16 = vld1q_f16(A);
        float32x4_t a_low = vcvt_f32_f16(vget_low_f16(a_f16));
        float32x4_t a_high = vcvt_f32_f16(vget_high_f16(a_f16));

        float16x8_t b_f16 = vld1q_f16(B);
        float32x4_t b_low = vcvt_f32_f16(vget_low_f16(b_f16));
        float32x4_t b_high = vcvt_f32_f16(vget_high_f16(b_f16));

        float32_t b0_lane0 = vgetq_lane_f32(b_low, 0);
        c0_low = vfmaq_n_f32(c0_low, a_low, b0_lane0);
        c0_high = vfmaq_n_f32(c0_high, a_high, b0_lane0);

        float32_t b0_lane1 = vgetq_lane_f32(b_low, 1);
        c1_low = vfmaq_n_f32(c1_low, a_low, b0_lane1);
        c1_high = vfmaq_n_f32(c1_high, a_high, b0_lane1);

        float32_t b0_lane2 = vgetq_lane_f32(b_low, 2);
        c2_low = vfmaq_n_f32(c2_low, a_low, b0_lane2);
        c2_high = vfmaq_n_f32(c2_high, a_high, b0_lane2);

        float32_t b0_lane3 = vgetq_lane_f32(b_low, 3);
        c3_low = vfmaq_n_f32(c3_low, a_low, b0_lane3);
        c3_high = vfmaq_n_f32(c3_high, a_high, b0_lane3);

        float32_t b1_lane0 = vgetq_lane_f32(b_high, 0);
        c4_low = vfmaq_n_f32(c4_low, a_low, b1_lane0);
        c4_high = vfmaq_n_f32(c4_high, a_high, b1_lane0);

        float32_t b1_lane1 = vgetq_lane_f32(b_high, 1);
        c5_low = vfmaq_n_f32(c5_low, a_low, b1_lane1);
        c5_high = vfmaq_n_f32(c5_high, a_high, b1_lane1);

        float32_t b1_lane2 = vgetq_lane_f32(b_high, 2);
        c6_low = vfmaq_n_f32(c6_low, a_low, b1_lane2);
        c6_high = vfmaq_n_f32(c6_high, a_high, b1_lane2);

        float32_t b1_lane3 = vgetq_lane_f32(b_high, 3);
        c7_low = vfmaq_n_f32(c7_low, a_low, b1_lane3);
        c7_high = vfmaq_n_f32(c7_high, a_high, b1_lane3);

        A += 8;
        B += 8;
    }

    FLOAT *col_0 = C + 0 * ldc;
    FLOAT *col_1 = C + 1 * ldc;
    FLOAT *col_2 = C + 2 * ldc;
    FLOAT *col_3 = C + 3 * ldc;
    FLOAT *col_4 = C + 4 * ldc;
    FLOAT *col_5 = C + 5 * ldc;
    FLOAT *col_6 = C + 6 * ldc;
    FLOAT *col_7 = C + 7 * ldc;

    float32x4_t t0_l = vld1q_f32(col_0);
    float32x4_t t0_h = vld1q_f32(col_0 + 4);
    t0_l = vaddq_f32(t0_l, vmulq_n_f32(c0_low, alpha));
    t0_h = vaddq_f32(t0_h, vmulq_n_f32(c0_high, alpha));
    vst1q_f32(col_0, t0_l);
    vst1q_f32(col_0 + 4, t0_h);

    float32x4_t t1_l = vld1q_f32(col_1);
    float32x4_t t1_h = vld1q_f32(col_1 + 4);
    t1_l = vaddq_f32(t1_l, vmulq_n_f32(c1_low, alpha));
    t1_h = vaddq_f32(t1_h, vmulq_n_f32(c1_high, alpha));
    vst1q_f32(col_1, t1_l);
    vst1q_f32(col_1 + 4, t1_h);

    float32x4_t t2_l = vld1q_f32(col_2);
    float32x4_t t2_h = vld1q_f32(col_2 + 4);
    t2_l = vaddq_f32(t2_l, vmulq_n_f32(c2_low, alpha));
    t2_h = vaddq_f32(t2_h, vmulq_n_f32(c2_high, alpha));
    vst1q_f32(col_2, t2_l);
    vst1q_f32(col_2 + 4, t2_h);

    float32x4_t t3_l = vld1q_f32(col_3);
    float32x4_t t3_h = vld1q_f32(col_3 + 4);
    t3_l = vaddq_f32(t3_l, vmulq_n_f32(c3_low, alpha));
    t3_h = vaddq_f32(t3_h, vmulq_n_f32(c3_high, alpha));
    vst1q_f32(col_3, t3_l);
    vst1q_f32(col_3 + 4, t3_h);

    float32x4_t t4_l = vld1q_f32(col_4);
    float32x4_t t4_h = vld1q_f32(col_4 + 4);
    t4_l = vaddq_f32(t4_l, vmulq_n_f32(c4_low, alpha));
    t4_h = vaddq_f32(t4_h, vmulq_n_f32(c4_high, alpha));
    vst1q_f32(col_4, t4_l);
    vst1q_f32(col_4 + 4, t4_h);

    float32x4_t t5_l = vld1q_f32(col_5);
    float32x4_t t5_h = vld1q_f32(col_5 + 4);
    t5_l = vaddq_f32(t5_l, vmulq_n_f32(c5_low, alpha));
    t5_h = vaddq_f32(t5_h, vmulq_n_f32(c5_high, alpha));
    vst1q_f32(col_5, t5_l);
    vst1q_f32(col_5 + 4, t5_h);

    float32x4_t t6_l = vld1q_f32(col_6);
    float32x4_t t6_h = vld1q_f32(col_6 + 4);
    t6_l = vaddq_f32(t6_l, vmulq_n_f32(c6_low, alpha));
    t6_h = vaddq_f32(t6_h, vmulq_n_f32(c6_high, alpha));
    vst1q_f32(col_6, t6_l);
    vst1q_f32(col_6 + 4, t6_h);

    float32x4_t t7_l = vld1q_f32(col_7);
    float32x4_t t7_h = vld1q_f32(col_7 + 4);
    t7_l = vaddq_f32(t7_l, vmulq_n_f32(c7_low, alpha));
    t7_h = vaddq_f32(t7_h, vmulq_n_f32(c7_high, alpha));
    vst1q_f32(col_7, t7_l);
    vst1q_f32(col_7 + 4, t7_h);
}

static inline void kernel_4x8(BLASLONG K, const float16_t *A, const float16_t *B, FLOAT *C, BLASLONG ldc, FLOAT alpha) {
    float32x4_t c0 = vdupq_n_f32(0.0f);
    float32x4_t c1 = vdupq_n_f32(0.0f);
    float32x4_t c2 = vdupq_n_f32(0.0f);
    float32x4_t c3 = vdupq_n_f32(0.0f);
    float32x4_t c4 = vdupq_n_f32(0.0f);
    float32x4_t c5 = vdupq_n_f32(0.0f);
    float32x4_t c6 = vdupq_n_f32(0.0f);
    float32x4_t c7 = vdupq_n_f32(0.0f);

    for (BLASLONG k = 0; k < K; ++k) {
        float32x4_t a_f16 = vcvt_f32_f16(vld1_f16(A));

        float16x8_t b_f16 = vld1q_f16(B);
        float32x4_t b_low = vcvt_f32_f16(vget_low_f16(b_f16));
        float32x4_t b_high = vcvt_f32_f16(vget_high_f16(b_f16));

        float32_t b0_lane0 = vgetq_lane_f32(b_low, 0);
        c0 = vfmaq_n_f32(c0, a_f16, b0_lane0);

        float32_t b0_lane1 = vgetq_lane_f32(b_low, 1);
        c1 = vfmaq_n_f32(c1, a_f16, b0_lane1);

        float32_t b0_lane2 = vgetq_lane_f32(b_low, 2);
        c2 = vfmaq_n_f32(c2, a_f16, b0_lane2);

        float32_t b0_lane3 = vgetq_lane_f32(b_low, 3);
        c3 = vfmaq_n_f32(c3, a_f16, b0_lane3);

        float32_t b1_lane0 = vgetq_lane_f32(b_high, 0);
        c4 = vfmaq_n_f32(c4, a_f16, b1_lane0);

        float32_t b1_lane1 = vgetq_lane_f32(b_high, 1);
        c5 = vfmaq_n_f32(c5, a_f16, b1_lane1);

        float32_t b1_lane2 = vgetq_lane_f32(b_high, 2);
        c6 = vfmaq_n_f32(c6, a_f16, b1_lane2);

        float32_t b1_lane3 = vgetq_lane_f32(b_high, 3);
        c7 = vfmaq_n_f32(c7, a_f16, b1_lane3);

        A += 4;
        B += 8;
    }

    FLOAT *col_0 = C + 0 * ldc;
    FLOAT *col_1 = C + 1 * ldc;
    FLOAT *col_2 = C + 2 * ldc;
    FLOAT *col_3 = C + 3 * ldc;
    FLOAT *col_4 = C + 4 * ldc;
    FLOAT *col_5 = C + 5 * ldc;
    FLOAT *col_6 = C + 6 * ldc;
    FLOAT *col_7 = C + 7 * ldc;

    float32x4_t t0 = vld1q_f32(col_0);
    t0 = vaddq_f32(t0, vmulq_n_f32(c0, alpha));
    vst1q_f32(col_0, t0);

    float32x4_t t1 = vld1q_f32(col_1);
    t1 = vaddq_f32(t1, vmulq_n_f32(c1, alpha));
    vst1q_f32(col_1, t1);

    float32x4_t t2 = vld1q_f32(col_2);
    t2 = vaddq_f32(t2, vmulq_n_f32(c2, alpha));
    vst1q_f32(col_2, t2);

    float32x4_t t3 = vld1q_f32(col_3);
    t3 = vaddq_f32(t3, vmulq_n_f32(c3, alpha));
    vst1q_f32(col_3, t3);

    float32x4_t t4 = vld1q_f32(col_4);
    t4 = vaddq_f32(t4, vmulq_n_f32(c4, alpha));
    vst1q_f32(col_4, t4);

    float32x4_t t5 = vld1q_f32(col_5);
    t5 = vaddq_f32(t5, vmulq_n_f32(c5, alpha));
    vst1q_f32(col_5, t5);

    float32x4_t t6 = vld1q_f32(col_6);
    t6 = vaddq_f32(t6, vmulq_n_f32(c6, alpha));
    vst1q_f32(col_6, t6);

    float32x4_t t7 = vld1q_f32(col_7);
    t7 = vaddq_f32(t7, vmulq_n_f32(c7, alpha));
    vst1q_f32(col_7, t7);
}

static inline void kernel_2x8(BLASLONG K, const float16_t *A, const float16_t *B, FLOAT *C, BLASLONG ldc, FLOAT alpha) {
    float32x2_t c0 = vdup_n_f32(0.0f);
    float32x2_t c1 = vdup_n_f32(0.0f);
    float32x2_t c2 = vdup_n_f32(0.0f);
    float32x2_t c3 = vdup_n_f32(0.0f);
    float32x2_t c4 = vdup_n_f32(0.0f);
    float32x2_t c5 = vdup_n_f32(0.0f);
    float32x2_t c6 = vdup_n_f32(0.0f);
    float32x2_t c7 = vdup_n_f32(0.0f);

    for (BLASLONG k = 0; k < K; ++k) {
        float32x4_t a_f32 = vcvt_f32_f16(vld1_f16(A));
        float32x2_t a_low = vget_low_f32(a_f32);

        float16x8_t b_f16 = vld1q_f16(B);
        float32x4_t b_low = vcvt_f32_f16(vget_low_f16(b_f16));
        float32x4_t b_high = vcvt_f32_f16(vget_high_f16(b_f16));

        float32_t b0_lane0 = vgetq_lane_f32(b_low, 0);
        c0 = vfma_n_f32(c0, a_low, b0_lane0);

        float32_t b0_lane1 = vgetq_lane_f32(b_low, 1);
        c1 = vfma_n_f32(c1, a_low, b0_lane1);

        float32_t b0_lane2 = vgetq_lane_f32(b_low, 2);
        c2 = vfma_n_f32(c2, a_low, b0_lane2);

        float32_t b0_lane3 = vgetq_lane_f32(b_low, 3);
        c3 = vfma_n_f32(c3, a_low, b0_lane3);

        float32_t b1_lane0 = vgetq_lane_f32(b_high, 0);
        c4 = vfma_n_f32(c4, a_low, b1_lane0);

        float32_t b1_lane1 = vgetq_lane_f32(b_high, 1);
        c5 = vfma_n_f32(c5, a_low, b1_lane1);

        float32_t b1_lane2 = vgetq_lane_f32(b_high, 2);
        c6 = vfma_n_f32(c6, a_low, b1_lane2);

        float32_t b1_lane3 = vgetq_lane_f32(b_high, 3);
        c7 = vfma_n_f32(c7, a_low, b1_lane3);

        A += 2;
        B += 8;
    }

    FLOAT *col_0 = C + 0 * ldc;
    FLOAT *col_1 = C + 1 * ldc;
    FLOAT *col_2 = C + 2 * ldc;
    FLOAT *col_3 = C + 3 * ldc;
    FLOAT *col_4 = C + 4 * ldc;
    FLOAT *col_5 = C + 5 * ldc;
    FLOAT *col_6 = C + 6 * ldc;
    FLOAT *col_7 = C + 7 * ldc;

    float32x2_t t0 = vld1_f32(col_0);
    t0 = vadd_f32(t0, vmul_n_f32(c0, alpha));
    vst1_f32(col_0, t0);

    float32x2_t t1 = vld1_f32(col_1);
    t1 = vadd_f32(t1, vmul_n_f32(c1, alpha));
    vst1_f32(col_1, t1);

    float32x2_t t2 = vld1_f32(col_2);
    t2 = vadd_f32(t2, vmul_n_f32(c2, alpha));
    vst1_f32(col_2, t2);

    float32x2_t t3 = vld1_f32(col_3);
    t3 = vadd_f32(t3, vmul_n_f32(c3, alpha));
    vst1_f32(col_3, t3);

    float32x2_t t4 = vld1_f32(col_4);
    t4 = vadd_f32(t4, vmul_n_f32(c4, alpha));
    vst1_f32(col_4, t4);

    float32x2_t t5 = vld1_f32(col_5);
    t5 = vadd_f32(t5, vmul_n_f32(c5, alpha));
    vst1_f32(col_5, t5);

    float32x2_t t6 = vld1_f32(col_6);
    t6 = vadd_f32(t6, vmul_n_f32(c6, alpha));
    vst1_f32(col_6, t6);

    float32x2_t t7 = vld1_f32(col_7);
    t7 = vadd_f32(t7, vmul_n_f32(c7, alpha));
    vst1_f32(col_7, t7);
}

static inline void kernel_1x8(BLASLONG K, const float16_t *A, const float16_t *B, FLOAT *C, BLASLONG ldc, FLOAT alpha) {
    FLOAT c0 = 0, c1 = 0, c2 = 0, c3 = 0, c4 = 0, c5 = 0, c6 = 0, c7 = 0;

    for (BLASLONG k = 0; k < K; ++k) {
        FLOAT a = A[0];
        c0 += a * B[0];
        c1 += a * B[1];
        c2 += a * B[2];
        c3 += a * B[3];
        c4 += a * B[4];
        c5 += a * B[5];
        c6 += a * B[6];
        c7 += a * B[7];

        A += 1;
        B += 8;
    }

    C[0 * ldc] += alpha * c0;
    C[1 * ldc] += alpha * c1;
    C[2 * ldc] += alpha * c2;
    C[3 * ldc] += alpha * c3;
    C[4 * ldc] += alpha * c4;
    C[5 * ldc] += alpha * c5;
    C[6 * ldc] += alpha * c6;
    C[7 * ldc] += alpha * c7;
}

static inline void kernel_8x4(BLASLONG K, const float16_t *A, const float16_t *B, FLOAT *C, BLASLONG ldc, FLOAT alpha) {
    float32x4_t c0_low = vdupq_n_f32(0.0f);
    float32x4_t c0_high = vdupq_n_f32(0.0f);
    float32x4_t c1_low = vdupq_n_f32(0.0f);
    float32x4_t c1_high = vdupq_n_f32(0.0f);
    float32x4_t c2_low = vdupq_n_f32(0.0f);
    float32x4_t c2_high = vdupq_n_f32(0.0f);
    float32x4_t c3_low = vdupq_n_f32(0.0f);
    float32x4_t c3_high = vdupq_n_f32(0.0f);

    for (BLASLONG k = 0; k < K; ++k) {
        float16x8_t a_f16 = vld1q_f16(A);
        float32x4_t a_low = vcvt_f32_f16(vget_low_f16(a_f16));
        float32x4_t a_high = vcvt_f32_f16(vget_high_f16(a_f16));

        float32x4_t b_f32 = vcvt_f32_f16(vld1_f16(B));

        float32_t b0_lane0 = vgetq_lane_f32(b_f32, 0);
        c0_low = vfmaq_n_f32(c0_low, a_low, b0_lane0);
        c0_high = vfmaq_n_f32(c0_high, a_high, b0_lane0);

        float32_t b0_lane1 = vgetq_lane_f32(b_f32, 1);
        c1_low = vfmaq_n_f32(c1_low, a_low, b0_lane1);
        c1_high = vfmaq_n_f32(c1_high, a_high, b0_lane1);

        float32_t b0_lane2 = vgetq_lane_f32(b_f32, 2);
        c2_low = vfmaq_n_f32(c2_low, a_low, b0_lane2);
        c2_high = vfmaq_n_f32(c2_high, a_high, b0_lane2);

        float32_t b0_lane3 = vgetq_lane_f32(b_f32, 3);
        c3_low = vfmaq_n_f32(c3_low, a_low, b0_lane3);
        c3_high = vfmaq_n_f32(c3_high, a_high, b0_lane3);

        A += 8;
        B += 4;
    }

    FLOAT *col_0 = C + 0 * ldc;
    FLOAT *col_1 = C + 1 * ldc;
    FLOAT *col_2 = C + 2 * ldc;
    FLOAT *col_3 = C + 3 * ldc;

    float32x4_t t0_l = vld1q_f32(col_0);
    float32x4_t t0_h = vld1q_f32(col_0 + 4);
    t0_l = vaddq_f32(t0_l, vmulq_n_f32(c0_low, alpha));
    t0_h = vaddq_f32(t0_h, vmulq_n_f32(c0_high, alpha));
    vst1q_f32(col_0, t0_l);
    vst1q_f32(col_0 + 4, t0_h);

    float32x4_t t1_l = vld1q_f32(col_1);
    float32x4_t t1_h = vld1q_f32(col_1 + 4);
    t1_l = vaddq_f32(t1_l, vmulq_n_f32(c1_low, alpha));
    t1_h = vaddq_f32(t1_h, vmulq_n_f32(c1_high, alpha));
    vst1q_f32(col_1, t1_l);
    vst1q_f32(col_1 + 4, t1_h);

    float32x4_t t2_l = vld1q_f32(col_2);
    float32x4_t t2_h = vld1q_f32(col_2 + 4);
    t2_l = vaddq_f32(t2_l, vmulq_n_f32(c2_low, alpha));
    t2_h = vaddq_f32(t2_h, vmulq_n_f32(c2_high, alpha));
    vst1q_f32(col_2, t2_l);
    vst1q_f32(col_2 + 4, t2_h);

    float32x4_t t3_l = vld1q_f32(col_3);
    float32x4_t t3_h = vld1q_f32(col_3 + 4);
    t3_l = vaddq_f32(t3_l, vmulq_n_f32(c3_low, alpha));
    t3_h = vaddq_f32(t3_h, vmulq_n_f32(c3_high, alpha));
    vst1q_f32(col_3, t3_l);
    vst1q_f32(col_3 + 4, t3_h);
}

static inline void kernel_4x4(BLASLONG K, const float16_t *A, const float16_t *B, FLOAT *C, BLASLONG ldc, FLOAT alpha) {
    float32x4_t c0 = vdupq_n_f32(0.0f);
    float32x4_t c1 = vdupq_n_f32(0.0f);
    float32x4_t c2 = vdupq_n_f32(0.0f);
    float32x4_t c3 = vdupq_n_f32(0.0f);

    for (BLASLONG k = 0; k < K; ++k) {
        float32x4_t a_f32 = vcvt_f32_f16(vld1_f16(A));
        float32x4_t b_f32 = vcvt_f32_f16(vld1_f16(B));

        float32_t b0_lane0 = vgetq_lane_f32(b_f32, 0);
        c0 = vfmaq_n_f32(c0, a_f32, b0_lane0);

        float32_t b0_lane1 = vgetq_lane_f32(b_f32, 1);
        c1 = vfmaq_n_f32(c1, a_f32, b0_lane1);

        float32_t b0_lane2 = vgetq_lane_f32(b_f32, 2);
        c2 = vfmaq_n_f32(c2, a_f32, b0_lane2);

        float32_t b0_lane3 = vgetq_lane_f32(b_f32, 3);
        c3 = vfmaq_n_f32(c3, a_f32, b0_lane3);

        A += 4;
        B += 4;
    }

    FLOAT *col_0 = C + 0 * ldc;
    FLOAT *col_1 = C + 1 * ldc;
    FLOAT *col_2 = C + 2 * ldc;
    FLOAT *col_3 = C + 3 * ldc;

    float32x4_t t0 = vld1q_f32(col_0);
    t0 = vaddq_f32(t0, vmulq_n_f32(c0, alpha));
    vst1q_f32(col_0, t0);

    float32x4_t t1 = vld1q_f32(col_1);
    t1 = vaddq_f32(t1, vmulq_n_f32(c1, alpha));
    vst1q_f32(col_1, t1);

    float32x4_t t2 = vld1q_f32(col_2);
    t2 = vaddq_f32(t2, vmulq_n_f32(c2, alpha));
    vst1q_f32(col_2, t2);

    float32x4_t t3 = vld1q_f32(col_3);
    t3 = vaddq_f32(t3, vmulq_n_f32(c3, alpha));
    vst1q_f32(col_3, t3);
}

static inline void kernel_2x4(BLASLONG K, const float16_t *A, const float16_t *B, FLOAT *C, BLASLONG ldc, FLOAT alpha) {
    float32x2_t c0 = vdup_n_f32(0.0f);
    float32x2_t c1 = vdup_n_f32(0.0f);
    float32x2_t c2 = vdup_n_f32(0.0f);
    float32x2_t c3 = vdup_n_f32(0.0f);

    for (BLASLONG k = 0; k < K; ++k) {
        float32x4_t a_f32 = vcvt_f32_f16(vld1_f16(A));
        float32x2_t a_low = vget_low_f32(a_f32);

        float32x4_t b_f32 = vcvt_f32_f16(vld1_f16(B));

        float32_t b0_lane0 = vgetq_lane_f32(b_f32, 0);
        c0 = vfma_n_f32(c0, a_low, b0_lane0);

        float32_t b0_lane1 = vgetq_lane_f32(b_f32, 1);
        c1 = vfma_n_f32(c1, a_low, b0_lane1);

        float32_t b0_lane2 = vgetq_lane_f32(b_f32, 2);
        c2 = vfma_n_f32(c2, a_low, b0_lane2);

        float32_t b0_lane3 = vgetq_lane_f32(b_f32, 3);
        c3 = vfma_n_f32(c3, a_low, b0_lane3);
        A += 2;
        B += 4;
    }

    FLOAT *col_0 = C + 0 * ldc;
    FLOAT *col_1 = C + 1 * ldc;
    FLOAT *col_2 = C + 2 * ldc;
    FLOAT *col_3 = C + 3 * ldc;

    float32x2_t t0 = vld1_f32(col_0);
    t0 = vadd_f32(t0, vmul_n_f32(c0, alpha));
    vst1_f32(col_0, t0);

    float32x2_t t1 = vld1_f32(col_1);
    t1 = vadd_f32(t1, vmul_n_f32(c1, alpha));
    vst1_f32(col_1, t1);

    float32x2_t t2 = vld1_f32(col_2);
    t2 = vadd_f32(t2, vmul_n_f32(c2, alpha));
    vst1_f32(col_2, t2);

    float32x2_t t3 = vld1_f32(col_3);
    t3 = vadd_f32(t3, vmul_n_f32(c3, alpha));
    vst1_f32(col_3, t3);
}

static inline void kernel_1x4(BLASLONG K, const float16_t *A, const float16_t *B, FLOAT *C, BLASLONG ldc, FLOAT alpha) {
    FLOAT c0 = 0, c1 = 0, c2 = 0, c3 = 0;
    for (BLASLONG k = 0; k < K; ++k) {
        FLOAT a = A[0];
        c0 += a * B[0];
        c1 += a * B[1];
        c2 += a * B[2];
        c3 += a * B[3];

        A += 1;
        B += 4;
    }

    C[0 * ldc] += alpha * c0;
    C[1 * ldc] += alpha * c1;
    C[2 * ldc] += alpha * c2;
    C[3 * ldc] += alpha * c3;
}

static inline void kernel_8x2(BLASLONG K, const float16_t *A, const float16_t *B, FLOAT *C, BLASLONG ldc, FLOAT alpha) {
    float32x4_t c0_low = vdupq_n_f32(0.0f);
    float32x4_t c0_high = vdupq_n_f32(0.0f);
    float32x4_t c1_low = vdupq_n_f32(0.0f);
    float32x4_t c1_high = vdupq_n_f32(0.0f);

    for (BLASLONG k = 0; k < K; ++k) {
        float16x8_t a_f16 = vld1q_f16(A);
        float32x4_t a_low = vcvt_f32_f16(vget_low_f16(a_f16));
        float32x4_t a_high = vcvt_f32_f16(vget_high_f16(a_f16));

        float32x4_t b_f32 = vcvt_f32_f16(vld1_f16(B));

        float32_t b0_lane0 = vgetq_lane_f32(b_f32, 0);
        c0_low = vfmaq_n_f32(c0_low, a_low, b0_lane0);
        c0_high = vfmaq_n_f32(c0_high, a_high, b0_lane0);

        float32_t b0_lane1 = vgetq_lane_f32(b_f32, 1);
        c1_low = vfmaq_n_f32(c1_low, a_low, b0_lane1);
        c1_high = vfmaq_n_f32(c1_high, a_high, b0_lane1);

        A += 8;
        B += 2;
    }

    FLOAT *col_0 = C + 0 * ldc;
    FLOAT *col_1 = C + 1 * ldc;

    float32x4_t t0_l = vld1q_f32(col_0);
    float32x4_t t0_h = vld1q_f32(col_0 + 4);
    t0_l = vaddq_f32(t0_l, vmulq_n_f32(c0_low, alpha));
    t0_h = vaddq_f32(t0_h, vmulq_n_f32(c0_high, alpha));
    vst1q_f32(col_0, t0_l);
    vst1q_f32(col_0 + 4, t0_h);

    float32x4_t t1_l = vld1q_f32(col_1);
    float32x4_t t1_h = vld1q_f32(col_1 + 4);
    t1_l = vaddq_f32(t1_l, vmulq_n_f32(c1_low, alpha));
    t1_h = vaddq_f32(t1_h, vmulq_n_f32(c1_high, alpha));
    vst1q_f32(col_1, t1_l);
    vst1q_f32(col_1 + 4, t1_h);
}

static inline void kernel_4x2(BLASLONG K, const float16_t *A, const float16_t *B, FLOAT *C, BLASLONG ldc, FLOAT alpha) {
    float32x4_t c0 = vdupq_n_f32(0.0f);
    float32x4_t c1 = vdupq_n_f32(0.0f);

    for (BLASLONG k = 0; k < K; ++k) {
        float32x4_t a_f32 = vcvt_f32_f16(vld1_f16(A));
        float32x4_t b_f32 = vcvt_f32_f16(vld1_f16(B));

        float32_t b0_lane0 = vgetq_lane_f32(b_f32, 0);
        c0 = vfmaq_n_f32(c0, a_f32, b0_lane0);

        float32_t b0_lane1 = vgetq_lane_f32(b_f32, 1);
        c1 = vfmaq_n_f32(c1, a_f32, b0_lane1);

        A += 4;
        B += 2;
    }

    FLOAT *col_0 = C + 0 * ldc;
    FLOAT *col_1 = C + 1 * ldc;

    float32x4_t t0 = vld1q_f32(col_0);
    t0 = vaddq_f32(t0, vmulq_n_f32(c0, alpha));
    vst1q_f32(col_0, t0);

    float32x4_t t1 = vld1q_f32(col_1);
    t1 = vaddq_f32(t1, vmulq_n_f32(c1, alpha));
    vst1q_f32(col_1, t1);
}

static inline void kernel_2x2(BLASLONG K, const float16_t *A, const float16_t *B, FLOAT *C, BLASLONG ldc, FLOAT alpha) {
    float32x2_t c0 = vdup_n_f32(0.0f);
    float32x2_t c1 = vdup_n_f32(0.0f);

    for (BLASLONG k = 0; k < K; ++k) {
        float32x4_t a_f32 = vcvt_f32_f16(vld1_f16(A));
        float32x2_t a_low = vget_low_f32(a_f32);

        float32x4_t b_f32 = vcvt_f32_f16(vld1_f16(B));

        float32_t b0_lane0 = vgetq_lane_f32(b_f32, 0);
        c0 = vfma_n_f32(c0, a_low, b0_lane0);

        float32_t b0_lane1 = vgetq_lane_f32(b_f32, 1);
        c1 = vfma_n_f32(c1, a_low, b0_lane1);
        ;

        A += 2;
        B += 2;
    }

    FLOAT *col_0 = C + 0 * ldc;
    FLOAT *col_1 = C + 1 * ldc;

    float32x2_t t0 = vld1_f32(col_0);
    t0 = vadd_f32(t0, vmul_n_f32(c0, alpha));
    vst1_f32(col_0, t0);

    float32x2_t t1 = vld1_f32(col_1);
    t1 = vadd_f32(t1, vmul_n_f32(c1, alpha));
    vst1_f32(col_1, t1);
}

static inline void kernel_1x2(BLASLONG K, const float16_t *A, const float16_t *B, FLOAT *C, BLASLONG ldc, FLOAT alpha) {
    FLOAT c0 = 0, c1 = 0;
    for (BLASLONG k = 0; k < K; ++k) {
        FLOAT a = A[0];
        c0 += a * B[0];
        c1 += a * B[1];

        A += 1;
        B += 2;
    }

    C[0 * ldc] += alpha * c0;
    C[1 * ldc] += alpha * c1;
}

static inline void kernel_8x1(BLASLONG K, const float16_t *A, const float16_t *B, FLOAT *C, FLOAT alpha) {
    float32x4_t c0_low = vdupq_n_f32(0.0f);
    float32x4_t c0_high = vdupq_n_f32(0.0f);

    for (BLASLONG k = 0; k < K; ++k) {
        float16x8_t a_f16 = vld1q_f16(A);
        float32x4_t a_low = vcvt_f32_f16(vget_low_f16(a_f16));
        float32x4_t a_high = vcvt_f32_f16(vget_high_f16(a_f16));

        float b_scalar = (float)B[0];

        c0_low = vfmaq_n_f32(c0_low, a_low, b_scalar);
        c0_high = vfmaq_n_f32(c0_high, a_high, b_scalar);

        A += 8;
        B += 1;
    }

    FLOAT *col_0 = C;

    float32x4_t t0_l = vld1q_f32(col_0);
    float32x4_t t0_h = vld1q_f32(col_0 + 4);
    t0_l = vaddq_f32(t0_l, vmulq_n_f32(c0_low, alpha));
    t0_h = vaddq_f32(t0_h, vmulq_n_f32(c0_high, alpha));
    vst1q_f32(col_0, t0_l);
    vst1q_f32(col_0 + 4, t0_h);
}

static inline void kernel_4x1(BLASLONG K, const float16_t *A, const float16_t *B, FLOAT *C, FLOAT alpha) {
    float32x4_t c0 = vdupq_n_f32(0.0f);

    for (BLASLONG k = 0; k < K; ++k) {
        float32x4_t a_f32 = vcvt_f32_f16(vld1_f16(A));
        float b_scalar = (float)B[0];
        c0 = vfmaq_n_f32(c0, a_f32, b_scalar);

        A += 4;
        B += 1;
    }

    FLOAT *col_0 = C;
    float32x4_t t0 = vld1q_f32(col_0);
    t0 = vaddq_f32(t0, vmulq_n_f32(c0, alpha));
    vst1q_f32(col_0, t0);
}

static inline void kernel_2x1(BLASLONG K, const float16_t *A, const float16_t *B, FLOAT *C, FLOAT alpha) {
    float32x2_t c0 = vdup_n_f32(0.0f);

    for (BLASLONG k = 0; k < K; ++k) {
        float32x4_t a_f32 = vcvt_f32_f16(vld1_f16(A));
        float32x2_t a_low = vget_low_f32(a_f32);

        float b_scalar = (float)B[0];
        c0 = vfma_n_f32(c0, a_low, b_scalar);

        A += 2;
        B += 1;
    }

    FLOAT *col_0 = C;
    float32x2_t t0 = vld1_f32(col_0);
    t0 = vadd_f32(t0, vmul_n_f32(c0, alpha));
    vst1_f32(col_0, t0);
}

static inline void kernel_1x1(BLASLONG K, const float16_t *A, const float16_t *B, FLOAT *C, FLOAT alpha) {
    FLOAT sum = 0.0f;
    for (BLASLONG k = 0; k < K; ++k) {
        sum += A[0] * B[0];
        A += 1;
        B += 1;
    }

    C[0] += alpha * sum;
}

int CNAME(BLASLONG M, BLASLONG N, BLASLONG K, FLOAT alpha, IFLOAT *A, IFLOAT *B, FLOAT *C, BLASLONG ldc) {
    float16_t *A_base = (float16_t *)A;
    float16_t *B_base = (float16_t *)B;

    FLOAT *Ccol = C;
    BLASLONG m_rem1, m_rem2, m_rem3, m_rem4;

    while (N >= 8) {
        const float16_t *Aptr = A_base;
        const float16_t *Bptr = B_base;
        FLOAT *Crow = Ccol;

        m_rem1 = M;

        while (m_rem1 >= 8) {
            kernel_8x8(K, Aptr, Bptr, Crow, ldc, alpha);
            Aptr += K * 8;
            Crow += 8;
            m_rem1 -= 8;
        }
        if (m_rem1 >= 4) {
            kernel_4x8(K, Aptr, Bptr, Crow, ldc, alpha);
            Aptr += K * 4;
            Crow += 4;
            m_rem1 -= 4;
        }
        if (m_rem1 >= 2) {
            kernel_2x8(K, Aptr, Bptr, Crow, ldc, alpha);
            Aptr += K * 2;
            Crow += 2;
            m_rem1 -= 2;
        }
        if (m_rem1 >= 1) {
            kernel_1x8(K, Aptr, Bptr, Crow, ldc, alpha);
        }

        B_base += K * 8;
        Ccol += ldc * 8;
        N -= 8;
    }

    if (N >= 4) {
        const float16_t *Aptr = A_base;
        const float16_t *Bptr = B_base;
        FLOAT *Crow = Ccol;

        m_rem2 = M;
        while (m_rem2 >= 8) {
            kernel_8x4(K, Aptr, Bptr, Crow, ldc, alpha);
            Aptr += K * 8;
            Crow += 8;
            m_rem2 -= 8;
        }
        if (m_rem2 >= 4) {
            kernel_4x4(K, Aptr, Bptr, Crow, ldc, alpha);
            Aptr += K * 4;
            Crow += 4;
            m_rem2 -= 4;
        }
        if (m_rem2 >= 2) {
            kernel_2x4(K, Aptr, Bptr, Crow, ldc, alpha);
            Aptr += K * 2;
            Crow += 2;
            m_rem2 -= 2;
        }
        if (m_rem2 >= 1) {
            kernel_1x4(K, Aptr, Bptr, Crow, ldc, alpha);
        }

        B_base += K * 4;
        Ccol += ldc * 4;
        N -= 4;
    }

    if (N >= 2) {
        const float16_t *Aptr = A_base;
        const float16_t *Bptr = B_base;
        FLOAT *Crow = Ccol;

        m_rem3 = M;
        while (m_rem3 >= 8) {
            kernel_8x2(K, Aptr, Bptr, Crow, ldc, alpha);
            Aptr += K * 8;
            Crow += 8;
            m_rem3 -= 8;
        }
        if (m_rem3 >= 4) {
            kernel_4x2(K, Aptr, Bptr, Crow, ldc, alpha);
            Aptr += K * 4;
            Crow += 4;
            m_rem3 -= 4;
        }
        if (m_rem3 >= 2) {
            kernel_2x2(K, Aptr, Bptr, Crow, ldc, alpha);
            Aptr += K * 2;
            Crow += 2;
            m_rem3 -= 2;
        }
        if (m_rem3 >= 1) {
            kernel_1x2(K, Aptr, Bptr, Crow, ldc, alpha);
        }

        B_base += K * 2;
        Ccol += ldc * 2;
        N -= 2;
    }

    if (N >= 1) {
        const float16_t *Aptr = A_base;
        const float16_t *Bptr = B_base;
        FLOAT *Crow = Ccol;

        m_rem4 = M;
        while (m_rem4 >= 8) {
            kernel_8x1(K, Aptr, Bptr, Crow, alpha);
            Aptr += K * 8;
            Crow += 8;
            m_rem4 -= 8;
        }
        if (m_rem4 >= 4) {
            kernel_4x1(K, Aptr, Bptr, Crow, alpha);
            Aptr += K * 4;
            Crow += 4;
            m_rem4 -= 4;
        }
        if (m_rem4 >= 2) {
            kernel_2x1(K, Aptr, Bptr, Crow, alpha);
            Aptr += K * 2;
            Crow += 2;
            m_rem4 -= 2;
        }
        if (m_rem4 >= 1) {
            kernel_1x1(K, Aptr, Bptr, Crow, alpha);
        }
    }

    return 0;
}