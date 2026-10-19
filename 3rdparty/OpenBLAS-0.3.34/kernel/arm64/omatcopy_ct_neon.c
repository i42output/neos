#include "common.h"

#include <arm_neon.h>
#ifndef DOUBLE
#define SimdWidth 8
#define transpose transpose8x8
#define transpose_a1 transpose8x8_a1
static __attribute__((always_inline)) void stnp_q(float *dst, float32x4_t a, float32x4_t b) {
    asm volatile("stnp %q[a], %q[b], [%[p]]" : : [a] "w"(a), [b] "w"(b), [p] "r"(dst) : "memory");
}

static __attribute__((always_inline)) inline void transpose8x8_a1(FLOAT *a,
                                                                  BLASLONG lda,
                                                                  FLOAT *b,
                                                                  BLASLONG ldb) {
    float32x4_t s00 = vld1q_f32(a + 0 * lda);
    float32x4_t s01 = vld1q_f32(a + 0 * lda + 4);
    float32x4_t s10 = vld1q_f32(a + 1 * lda);
    float32x4_t s11 = vld1q_f32(a + 1 * lda + 4);

    float32x4_t s20 = vld1q_f32(a + 2 * lda);
    float32x4_t s21 = vld1q_f32(a + 2 * lda + 4);
    float32x4_t s30 = vld1q_f32(a + 3 * lda);
    float32x4_t s31 = vld1q_f32(a + 3 * lda + 4);

    float32x4_t s40 = vld1q_f32(a + 4 * lda);
    float32x4_t s41 = vld1q_f32(a + 4 * lda + 4);
    float32x4_t s50 = vld1q_f32(a + 5 * lda);
    float32x4_t s51 = vld1q_f32(a + 5 * lda + 4);

    float32x4_t s60 = vld1q_f32(a + 6 * lda);
    float32x4_t s61 = vld1q_f32(a + 6 * lda + 4);
    float32x4_t s70 = vld1q_f32(a + 7 * lda);
    float32x4_t s71 = vld1q_f32(a + 7 * lda + 4);

    float32x4_t t00 = vzip1q_f32(s00, s10);  // 00 10 01 11
    float32x4_t t01 = vzip2q_f32(s00, s10);  // 02 12 03 13
    float32x4_t t10 = vzip1q_f32(s01, s11);  // 04 14 05 15
    float32x4_t t11 = vzip2q_f32(s01, s11);  // 06 16 07 17

    float32x4_t t20 = vzip1q_f32(s20, s30);  // 20 30 21 31
    float32x4_t t21 = vzip2q_f32(s20, s30);  // 22 32 23 33
    float32x4_t t30 = vzip1q_f32(s21, s31);  // 24 34 25 35
    float32x4_t t31 = vzip2q_f32(s21, s31);  // 26 36 27 37

    float32x4_t t40 = vzip1q_f32(s40, s50);  // 40 50 41 51
    float32x4_t t41 = vzip2q_f32(s40, s50);  // 42 52 43 53
    float32x4_t t50 = vzip1q_f32(s41, s51);  // 44 54 45 55
    float32x4_t t51 = vzip2q_f32(s41, s51);  // 46 56 47 57

    float32x4_t t60 = vzip1q_f32(s60, s70);  // 60 70 61 71
    float32x4_t t61 = vzip2q_f32(s60, s70);  // 62 72 63 73
    float32x4_t t70 = vzip1q_f32(s61, s71);  // 64 74 65 75
    float32x4_t t71 = vzip2q_f32(s61, s71);  // 66 76 67 77

    float32x4_t m00 = vcombine_f32(vget_low_f32(t00), vget_low_f32(t20));  // 00 10 20 30
    float32x4_t m01 = vcombine_f32(vget_low_f32(t40), vget_low_f32(t60));  // 40 50 60 70

    float32x4_t m10 = vcombine_f32(vget_high_f32(t00), vget_high_f32(t20));  // 01 11 21 31
    float32x4_t m11 = vcombine_f32(vget_high_f32(t40), vget_high_f32(t60));  // 41 51 61 71

    float32x4_t m20 = vcombine_f32(vget_low_f32(t01), vget_low_f32(t21));  // 02 12 22 32
    float32x4_t m21 = vcombine_f32(vget_low_f32(t41), vget_low_f32(t61));  // 42 52 62 72

    float32x4_t m30 = vcombine_f32(vget_high_f32(t01), vget_high_f32(t21));  // 03 13 23 33
    float32x4_t m31 = vcombine_f32(vget_high_f32(t41), vget_high_f32(t61));  // 43 53 63 73

    float32x4_t m40 = vcombine_f32(vget_low_f32(t10), vget_low_f32(t30));  // 04 14 24 34
    float32x4_t m41 = vcombine_f32(vget_low_f32(t50), vget_low_f32(t70));  // 44 54 64 74

    float32x4_t m50 = vcombine_f32(vget_high_f32(t10), vget_high_f32(t30));  // 05 15 25 35
    float32x4_t m51 = vcombine_f32(vget_high_f32(t50), vget_high_f32(t70));  // 45 55 65 75

    float32x4_t m60 = vcombine_f32(vget_low_f32(t11), vget_low_f32(t31));  // 06 16 26 36
    float32x4_t m61 = vcombine_f32(vget_low_f32(t51), vget_low_f32(t71));  // 46 56 66 76

    float32x4_t m70 = vcombine_f32(vget_high_f32(t11), vget_high_f32(t31));  // 07 17 27 37
    float32x4_t m71 = vcombine_f32(vget_high_f32(t51), vget_high_f32(t71));  // 47 57 67 77

    stnp_q(b + 0 * ldb, m00, m01);
    stnp_q(b + 1 * ldb, m10, m11);

    stnp_q(b + 2 * ldb, m20, m21);
    stnp_q(b + 3 * ldb, m30, m31);

    stnp_q(b + 4 * ldb, m40, m41);
    stnp_q(b + 5 * ldb, m50, m51);

    stnp_q(b + 6 * ldb, m60, m61);
    stnp_q(b + 7 * ldb, m70, m71);
}

static __attribute__((always_inline)) inline void transpose8x8(FLOAT alpha,
                                                               FLOAT *a,
                                                               BLASLONG lda,
                                                               FLOAT *b,
                                                               BLASLONG ldb) {
    float32x4_t s00 = vld1q_f32(a + 0 * lda);
    float32x4_t s01 = vld1q_f32(a + 0 * lda + 4);
    float32x4_t s10 = vld1q_f32(a + 1 * lda);
    float32x4_t s11 = vld1q_f32(a + 1 * lda + 4);

    float32x4_t s20 = vld1q_f32(a + 2 * lda);
    float32x4_t s21 = vld1q_f32(a + 2 * lda + 4);
    float32x4_t s30 = vld1q_f32(a + 3 * lda);
    float32x4_t s31 = vld1q_f32(a + 3 * lda + 4);

    float32x4_t s40 = vld1q_f32(a + 4 * lda);
    float32x4_t s41 = vld1q_f32(a + 4 * lda + 4);
    float32x4_t s50 = vld1q_f32(a + 5 * lda);
    float32x4_t s51 = vld1q_f32(a + 5 * lda + 4);

    float32x4_t s60 = vld1q_f32(a + 6 * lda);
    float32x4_t s61 = vld1q_f32(a + 6 * lda + 4);
    float32x4_t s70 = vld1q_f32(a + 7 * lda);
    float32x4_t s71 = vld1q_f32(a + 7 * lda + 4);

    float32x4_t t00 = vzip1q_f32(s00, s10);  // 00 10 01 11
    float32x4_t t01 = vzip2q_f32(s00, s10);  // 02 12 03 13
    float32x4_t t10 = vzip1q_f32(s01, s11);  // 04 14 05 15
    float32x4_t t11 = vzip2q_f32(s01, s11);  // 06 16 07 17

    float32x4_t t20 = vzip1q_f32(s20, s30);  // 20 30 21 31
    float32x4_t t21 = vzip2q_f32(s20, s30);  // 22 32 23 33
    float32x4_t t30 = vzip1q_f32(s21, s31);  // 24 34 25 35
    float32x4_t t31 = vzip2q_f32(s21, s31);  // 26 36 27 37

    float32x4_t t40 = vzip1q_f32(s40, s50);  // 40 50 41 51
    float32x4_t t41 = vzip2q_f32(s40, s50);  // 42 52 43 53
    float32x4_t t50 = vzip1q_f32(s41, s51);  // 44 54 45 55
    float32x4_t t51 = vzip2q_f32(s41, s51);  // 46 56 47 57

    float32x4_t t60 = vzip1q_f32(s60, s70);  // 60 70 61 71
    float32x4_t t61 = vzip2q_f32(s60, s70);  // 62 72 63 73
    float32x4_t t70 = vzip1q_f32(s61, s71);  // 64 74 65 75
    float32x4_t t71 = vzip2q_f32(s61, s71);  // 66 76 67 77

    float32x4_t m00 = vcombine_f32(vget_low_f32(t00), vget_low_f32(t20));  // 00 10 20 30
    float32x4_t m01 = vcombine_f32(vget_low_f32(t40), vget_low_f32(t60));  // 40 50 60 70

    float32x4_t m10 = vcombine_f32(vget_high_f32(t00), vget_high_f32(t20));  // 01 11 21 31
    float32x4_t m11 = vcombine_f32(vget_high_f32(t40), vget_high_f32(t60));  // 41 51 61 71

    float32x4_t m20 = vcombine_f32(vget_low_f32(t01), vget_low_f32(t21));  // 02 12 22 32
    float32x4_t m21 = vcombine_f32(vget_low_f32(t41), vget_low_f32(t61));  // 42 52 62 72

    float32x4_t m30 = vcombine_f32(vget_high_f32(t01), vget_high_f32(t21));  // 03 13 23 33
    float32x4_t m31 = vcombine_f32(vget_high_f32(t41), vget_high_f32(t61));  // 43 53 63 73

    float32x4_t m40 = vcombine_f32(vget_low_f32(t10), vget_low_f32(t30));  // 04 14 24 34
    float32x4_t m41 = vcombine_f32(vget_low_f32(t50), vget_low_f32(t70));  // 44 54 64 74

    float32x4_t m50 = vcombine_f32(vget_high_f32(t10), vget_high_f32(t30));  // 05 15 25 35
    float32x4_t m51 = vcombine_f32(vget_high_f32(t50), vget_high_f32(t70));  // 45 55 65 75

    float32x4_t m60 = vcombine_f32(vget_low_f32(t11), vget_low_f32(t31));  // 06 16 26 36
    float32x4_t m61 = vcombine_f32(vget_low_f32(t51), vget_low_f32(t71));  // 46 56 66 76

    float32x4_t m70 = vcombine_f32(vget_high_f32(t11), vget_high_f32(t31));  // 07 17 27 37
    float32x4_t m71 = vcombine_f32(vget_high_f32(t51), vget_high_f32(t71));  // 47 57 67 77

    float32x4_t valpha = vdupq_n_f32(alpha);

    m00 = vmulq_f32(m00, valpha);
    m01 = vmulq_f32(m01, valpha);
    m10 = vmulq_f32(m10, valpha);
    m11 = vmulq_f32(m11, valpha);
    m20 = vmulq_f32(m20, valpha);
    m21 = vmulq_f32(m21, valpha);
    m30 = vmulq_f32(m30, valpha);
    m31 = vmulq_f32(m31, valpha);
    m40 = vmulq_f32(m40, valpha);
    m41 = vmulq_f32(m41, valpha);
    m50 = vmulq_f32(m50, valpha);
    m51 = vmulq_f32(m51, valpha);
    m60 = vmulq_f32(m60, valpha);
    m61 = vmulq_f32(m61, valpha);
    m70 = vmulq_f32(m70, valpha);
    m71 = vmulq_f32(m71, valpha);

    stnp_q(b + 0 * ldb, m00, m01);
    stnp_q(b + 1 * ldb, m10, m11);

    stnp_q(b + 2 * ldb, m20, m21);
    stnp_q(b + 3 * ldb, m30, m31);

    stnp_q(b + 4 * ldb, m40, m41);
    stnp_q(b + 5 * ldb, m50, m51);

    stnp_q(b + 6 * ldb, m60, m61);
    stnp_q(b + 7 * ldb, m70, m71);
}

#else
#define SimdWidth 4
#define transpose transpose4x4
#define transpose_a1 transpose4x4_a1
static __attribute__((always_inline)) void stnp_q(double *dst, float64x2_t a, float64x2_t b) {
    asm volatile("stnp %q[a], %q[b], [%[p]]" : : [a] "w"(a), [b] "w"(b), [p] "r"(dst) : "memory");
}
static __attribute__((always_inline)) inline void transpose4x4(FLOAT alpha,
                                                               FLOAT *a,
                                                               BLASLONG lda,
                                                               FLOAT *b,
                                                               BLASLONG ldb) {
    float64x2_t s00 = vld1q_f64(a + 0 * lda);      // 00 01
    float64x2_t s01 = vld1q_f64(a + 0 * lda + 2);  // 02 03

    float64x2_t s10 = vld1q_f64(a + 1 * lda);      // 10 11
    float64x2_t s11 = vld1q_f64(a + 1 * lda + 2);  // 12 13

    float64x2_t s20 = vld1q_f64(a + 2 * lda);      // 20 21
    float64x2_t s21 = vld1q_f64(a + 2 * lda + 2);  // 22 23

    float64x2_t s30 = vld1q_f64(a + 3 * lda);      // 30 31
    float64x2_t s31 = vld1q_f64(a + 3 * lda + 2);  // 32 33

    float64x2_t t00 = vzip1q_f64(s00, s10);  // 00 10
    float64x2_t t01 = vzip2q_f64(s00, s10);  // 01 11

    float64x2_t t10 = vzip1q_f64(s01, s11);  // 02 12
    float64x2_t t11 = vzip2q_f64(s01, s11);  // 03 13

    float64x2_t t20 = vzip1q_f64(s20, s30);  // 20 30
    float64x2_t t21 = vzip2q_f64(s20, s30);  // 21 31

    float64x2_t t30 = vzip1q_f64(s21, s31);  // 22 32
    float64x2_t t31 = vzip2q_f64(s21, s31);  // 23 33

    float64x2_t v_alpha = vdupq_n_f64(alpha);
    t00 = vmulq_f64(t00, v_alpha);
    t20 = vmulq_f64(t20, v_alpha);
    t01 = vmulq_f64(t01, v_alpha);
    t21 = vmulq_f64(t21, v_alpha);
    t10 = vmulq_f64(t10, v_alpha);
    t30 = vmulq_f64(t30, v_alpha);
    t11 = vmulq_f64(t11, v_alpha);
    t31 = vmulq_f64(t31, v_alpha);

    stnp_q(b + 0 * ldb, t00, t20);
    stnp_q(b + 1 * ldb, t01, t21);
    stnp_q(b + 2 * ldb, t10, t30);
    stnp_q(b + 3 * ldb, t11, t31);
}
static __attribute__((always_inline)) inline void transpose4x4_a1(FLOAT *a,
                                                                  BLASLONG lda,
                                                                  FLOAT *b,
                                                                  BLASLONG ldb) {
    float64x2_t s00 = vld1q_f64(a + 0 * lda);      // 00 01
    float64x2_t s01 = vld1q_f64(a + 0 * lda + 2);  // 02 03

    float64x2_t s10 = vld1q_f64(a + 1 * lda);      // 10 11
    float64x2_t s11 = vld1q_f64(a + 1 * lda + 2);  // 12 13

    float64x2_t s20 = vld1q_f64(a + 2 * lda);      // 20 21
    float64x2_t s21 = vld1q_f64(a + 2 * lda + 2);  // 22 23

    float64x2_t s30 = vld1q_f64(a + 3 * lda);      // 30 31
    float64x2_t s31 = vld1q_f64(a + 3 * lda + 2);  // 32 33

    float64x2_t t00 = vzip1q_f64(s00, s10);  // 00 10
    float64x2_t t01 = vzip2q_f64(s00, s10);  // 01 11

    float64x2_t t10 = vzip1q_f64(s01, s11);  // 02 12
    float64x2_t t11 = vzip2q_f64(s01, s11);  // 03 13

    float64x2_t t20 = vzip1q_f64(s20, s30);  // 20 30
    float64x2_t t21 = vzip2q_f64(s20, s30);  // 21 31

    float64x2_t t30 = vzip1q_f64(s21, s31);  // 22 32
    float64x2_t t31 = vzip2q_f64(s21, s31);  // 23 33

    stnp_q(b + 0 * ldb, t00, t20);
    stnp_q(b + 1 * ldb, t01, t21);
    stnp_q(b + 2 * ldb, t10, t30);
    stnp_q(b + 3 * ldb, t11, t31);
}
#endif

int CNAME(BLASLONG rows,
          BLASLONG cols,
          FLOAT alpha,
          FLOAT *a,
          BLASLONG lda,
          FLOAT *b,
          BLASLONG ldb) {
    if (rows < SimdWidth || cols < SimdWidth) {
        for (BLASLONG i = 0; i < rows; ++i) {
            for (BLASLONG j = 0; j < cols; ++j) {
                b[i * ldb + j] = alpha * a[j * lda + i];
            }
        }
        return 0;
    }
    if (alpha == 0.0) {
        for (BLASLONG i = 0; i < rows; ++i) {
            for (BLASLONG j = 0; j < cols; ++j) {
                b[i * ldb + j] = 0.0;
            }
        }
        return 0;
    }
    if (alpha == 1.0) {
        BLASLONG j = 0;
        for (; j + 4 * SimdWidth <= cols; j += 4 * SimdWidth) {
            BLASLONG i = 0;
            for (; i + SimdWidth <= rows; i += SimdWidth) {
                transpose_a1(a + (j + 0 * SimdWidth) * lda + (i + 0),
                             lda,
                             b + (i + 0) * ldb + j + 0 * SimdWidth,
                             ldb);
                transpose_a1(a + (j + 1 * SimdWidth) * lda + (i + 0),
                             lda,
                             b + (i + 0) * ldb + j + 1 * SimdWidth,
                             ldb);
                transpose_a1(a + (j + 2 * SimdWidth) * lda + (i + 0),
                             lda,
                             b + (i + 0) * ldb + j + 2 * SimdWidth,
                             ldb);
                transpose_a1(a + (j + 3 * SimdWidth) * lda + (i + 0),
                             lda,
                             b + (i + 0) * ldb + j + 3 * SimdWidth,
                             ldb);
            }
            if (i != rows) {
                i = rows - SimdWidth;
                transpose_a1(a + (j + 0 * SimdWidth) * lda + (i + 0),
                             lda,
                             b + (i + 0) * ldb + j + 0 * SimdWidth,
                             ldb);
                transpose_a1(a + (j + 1 * SimdWidth) * lda + (i + 0),
                             lda,
                             b + (i + 0) * ldb + j + 1 * SimdWidth,
                             ldb);
                transpose_a1(a + (j + 2 * SimdWidth) * lda + (i + 0),
                             lda,
                             b + (i + 0) * ldb + j + 2 * SimdWidth,
                             ldb);
                transpose_a1(a + (j + 3 * SimdWidth) * lda + (i + 0),
                             lda,
                             b + (i + 0) * ldb + j + 3 * SimdWidth,
                             ldb);
            }
        }
        for (; j + SimdWidth <= cols; j += SimdWidth) {
            BLASLONG i = 0;
            for (; i + SimdWidth <= rows; i += SimdWidth) {
                transpose_a1(a + j * lda + i, lda, b + i * ldb + j, ldb);
            }
            if (i != rows) {
                i = rows - SimdWidth;
                transpose_a1(a + j * lda + i, lda, b + i * ldb + j, ldb);
            }
        }
        if (j != cols) {
            j = cols - SimdWidth;

            BLASLONG i = 0;
            for (; i + SimdWidth <= rows; i += SimdWidth) {
                transpose_a1(a + j * lda + i, lda, b + i * ldb + j, ldb);
            }
            if (i != rows) {
                i = rows - SimdWidth;
                transpose_a1(a + j * lda + i, lda, b + i * ldb + j, ldb);
            }
        }
        return 0;
    }
    BLASLONG j = 0;
    for (; j + 4 * SimdWidth <= cols; j += 4 * SimdWidth) {
        BLASLONG i = 0;
        for (; i + SimdWidth <= rows; i += SimdWidth) {
            transpose(alpha,
                      a + (j + 0 * SimdWidth) * lda + (i + 0),
                      lda,
                      b + (i + 0) * ldb + j + 0 * SimdWidth,
                      ldb);
            transpose(alpha,
                      a + (j + 1 * SimdWidth) * lda + (i + 0),
                      lda,
                      b + (i + 0) * ldb + j + 1 * SimdWidth,
                      ldb);
            transpose(alpha,
                      a + (j + 2 * SimdWidth) * lda + (i + 0),
                      lda,
                      b + (i + 0) * ldb + j + 2 * SimdWidth,
                      ldb);
            transpose(alpha,
                      a + (j + 3 * SimdWidth) * lda + (i + 0),
                      lda,
                      b + (i + 0) * ldb + j + 3 * SimdWidth,
                      ldb);
        }
        if (i != rows) {
            i = rows - SimdWidth;
            transpose(alpha,
                      a + (j + 0 * SimdWidth) * lda + (i + 0),
                      lda,
                      b + (i + 0) * ldb + j + 0 * SimdWidth,
                      ldb);
            transpose(alpha,
                      a + (j + 1 * SimdWidth) * lda + (i + 0),
                      lda,
                      b + (i + 0) * ldb + j + 1 * SimdWidth,
                      ldb);
            transpose(alpha,
                      a + (j + 2 * SimdWidth) * lda + (i + 0),
                      lda,
                      b + (i + 0) * ldb + j + 2 * SimdWidth,
                      ldb);
            transpose(alpha,
                      a + (j + 3 * SimdWidth) * lda + (i + 0),
                      lda,
                      b + (i + 0) * ldb + j + 3 * SimdWidth,
                      ldb);
        }
    }
    for (; j + SimdWidth <= cols; j += SimdWidth) {
        BLASLONG i = 0;
        for (; i + SimdWidth <= rows; i += SimdWidth) {
            transpose(alpha, a + j * lda + i, lda, b + i * ldb + j, ldb);
        }
        if (i != rows) {
            i = rows - SimdWidth;
            transpose(alpha, a + j * lda + i, lda, b + i * ldb + j, ldb);
        }
    }
    if (j != cols) {
        j = cols - SimdWidth;

        BLASLONG i = 0;
        for (; i + SimdWidth <= rows; i += SimdWidth) {
            transpose(alpha, a + j * lda + i, lda, b + i * ldb + j, ldb);
        }
        if (i != rows) {
            i = rows - SimdWidth;
            transpose(alpha, a + j * lda + i, lda, b + i * ldb + j, ldb);
        }
    }

    return 0;
}
