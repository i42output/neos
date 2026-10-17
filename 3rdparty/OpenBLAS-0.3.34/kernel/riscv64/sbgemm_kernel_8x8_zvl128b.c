#include "common.h"
#include <riscv_vector.h>

#define BF16_WIDEN_ONE  // Convert pre-hand and do operations in FP32
#define USE_BF16_CVT    // Comment out for pre-RVA23 systems

#ifdef BF16_WIDEN_ONE
#define FORCEINLINE      inline __attribute__((always_inline))
#define B_UNROLL         32

// Convert from BF16 to FP32
static void FORCEINLINE B_CONV(__bf16 *BB, FLOAT *CONV, BLASLONG count)
{
    BLASLONG count2 = (count & (B_UNROLL - 1));
    count &= -B_UNROLL;
    while (count) {
        vbfloat16m4_t B00 = __riscv_vle16_v_bf16m4(BB, B_UNROLL);
#ifdef USE_BF16_CVT
        vfloat32m8_t B0 = __riscv_vfwcvtbf16_f_f_v_f32m8(B00, B_UNROLL);
#else
        vfloat32m8_t B0 = __riscv_vreinterpret_v_u32m8_f32m8(__riscv_vsll_vx_u32m8(
            __riscv_vwcvtu_x_x_v_u32m8(__riscv_vreinterpret_v_bf16m4_u16m4(B00), B_UNROLL), 16, B_UNROLL));
#endif
        __riscv_vse32_v_f32m8(CONV, B0, B_UNROLL);
        BB += B_UNROLL;
        CONV += B_UNROLL;
        count -= B_UNROLL;
    }
    if (count2) {
        BLASLONG gvl2 = __riscv_vsetvl_e16m4(count2);
        vbfloat16m4_t B00 = __riscv_vle16_v_bf16m4(BB, gvl2);
#ifdef USE_BF16_CVT
        vfloat32m8_t B0 = __riscv_vfwcvtbf16_f_f_v_f32m8(B00, gvl2);
#else
        vfloat32m8_t B0 = __riscv_vreinterpret_v_u32m8_f32m8(__riscv_vsll_vx_u32m8(
            __riscv_vwcvtu_x_x_v_u32m8(__riscv_vreinterpret_v_bf16m4_u16m4(B00), gvl2), 16, gvl2));
#endif
        __riscv_vse32_v_f32m8(CONV, B0, gvl2);
    }
}
#endif

#ifndef VECTORIZE_MEMSET
#define memset_zero(ptr, size, dir)  memset(ptr, 0, size)
#else
void memset_zero(void *input, BLASLONG size, bool dir);
#endif

int CNAME(BLASLONG M, BLASLONG N, BLASLONG K, FLOAT alpha, IFLOAT *A, IFLOAT *B, FLOAT *C, BLASLONG ldc)
{
    BLASLONG gvl = 0;
    BLASLONG m_top = 0;
    BLASLONG n_top = 0;
#if !defined(BF16_WIDEN_ONE) || !defined(BF16_DONT_CONV)
    __bf16 *BB = (__bf16 *)(B);
    __bf16 *AA = (__bf16 *)(A);
#endif

#ifdef BF16_WIDEN_ONE
    FLOAT *CONV = (FLOAT *)(malloc((K * (8 + M)) * sizeof(FLOAT)));
    if (!CONV) return 1;
#ifndef BF16_DONT_CONV
    B_CONV(AA, CONV + (K * 8), K * M);
#else
    memset_zero(CONV, (K * (8 + M)) * sizeof(FLOAT), false);
#endif
#endif

    // -- MAIN PASS
    for (BLASLONG j=0; j<N/8; j+=1) {
        m_top = 0;
        BLASLONG gvl = __riscv_vsetvl_e16m1(8);

#ifdef BF16_WIDEN_ONE
        BLASLONG bi2 = K * 8;
#ifndef BF16_DONT_CONV
        B_CONV(BB + (n_top*K), CONV, bi2);
#endif
        BLASLONG ai2 = K * 8;
#endif

        for (BLASLONG i=0; i<M/8; i+=1) {
#ifdef BF16_WIDEN_ONE
            bi2 = 0;
#else
            BLASLONG ai=m_top*K;
            BLASLONG bi=n_top*K;
#endif

            vfloat32m2_t result0 = __riscv_vfmv_v_f_f32m2(0.0f, gvl);
            vfloat32m2_t result1 = __riscv_vfmv_v_f_f32m2(0.0f, gvl);
            vfloat32m2_t result2 = __riscv_vfmv_v_f_f32m2(0.0f, gvl);
            vfloat32m2_t result3 = __riscv_vfmv_v_f_f32m2(0.0f, gvl);
            vfloat32m2_t result4 = __riscv_vfmv_v_f_f32m2(0.0f, gvl);
            vfloat32m2_t result5 = __riscv_vfmv_v_f_f32m2(0.0f, gvl);
            vfloat32m2_t result6 = __riscv_vfmv_v_f_f32m2(0.0f, gvl);
            vfloat32m2_t result7 = __riscv_vfmv_v_f_f32m2(0.0f, gvl);

            for (BLASLONG k=0; k<K; k++) {
#ifdef BF16_WIDEN_ONE
                float B0 = CONV[bi2+0];
                float B1 = CONV[bi2+1];
                float B2 = CONV[bi2+2];
                float B3 = CONV[bi2+3];
                float B4 = CONV[bi2+4];
                float B5 = CONV[bi2+5];
                float B6 = CONV[bi2+6];
                float B7 = CONV[bi2+7];
                bi2 += 8;

                vfloat32m2_t A0 = __riscv_vle32_v_f32m2(&CONV[ai2], gvl);
                ai2 += 8;

                result0 = __riscv_vfmacc_vf_f32m2(result0, B0, A0, gvl);
                result1 = __riscv_vfmacc_vf_f32m2(result1, B1, A0, gvl);
                result2 = __riscv_vfmacc_vf_f32m2(result2, B2, A0, gvl);
                result3 = __riscv_vfmacc_vf_f32m2(result3, B3, A0, gvl);
                result4 = __riscv_vfmacc_vf_f32m2(result4, B4, A0, gvl);
                result5 = __riscv_vfmacc_vf_f32m2(result5, B5, A0, gvl);
                result6 = __riscv_vfmacc_vf_f32m2(result6, B6, A0, gvl);
                result7 = __riscv_vfmacc_vf_f32m2(result7, B7, A0, gvl);
#else
                __bf16 B0 = BB[bi+0];
                __bf16 B1 = BB[bi+1];
                __bf16 B2 = BB[bi+2];
                __bf16 B3 = BB[bi+3];
                __bf16 B4 = BB[bi+4];
                __bf16 B5 = BB[bi+5];
                __bf16 B6 = BB[bi+6];
                __bf16 B7 = BB[bi+7];
                bi += 8;

                vbfloat16m1_t A0 = __riscv_vle16_v_bf16m1( &AA[ai+0*gvl], gvl );
                ai += 8;

                result0 = __riscv_vfwmaccbf16_vf_f32m2(result0, B0, A0, gvl);
                result1 = __riscv_vfwmaccbf16_vf_f32m2(result1, B1, A0, gvl);
                result2 = __riscv_vfwmaccbf16_vf_f32m2(result2, B2, A0, gvl);
                result3 = __riscv_vfwmaccbf16_vf_f32m2(result3, B3, A0, gvl);
                result4 = __riscv_vfwmaccbf16_vf_f32m2(result4, B4, A0, gvl);
                result5 = __riscv_vfwmaccbf16_vf_f32m2(result5, B5, A0, gvl);
                result6 = __riscv_vfwmaccbf16_vf_f32m2(result6, B6, A0, gvl);
                result7 = __riscv_vfwmaccbf16_vf_f32m2(result7, B7, A0, gvl);
#endif
            }

            BLASLONG ci=n_top*ldc+m_top;

            vfloat32m2_t c0 = __riscv_vle32_v_f32m2( &C[ci], gvl); ci += ldc-gvl*0;
            vfloat32m2_t c1 = __riscv_vle32_v_f32m2( &C[ci], gvl); ci += ldc-gvl*0;
            vfloat32m2_t c2 = __riscv_vle32_v_f32m2( &C[ci], gvl); ci += ldc-gvl*0;
            vfloat32m2_t c3 = __riscv_vle32_v_f32m2( &C[ci], gvl); ci += ldc-gvl*0;
            vfloat32m2_t c4 = __riscv_vle32_v_f32m2( &C[ci], gvl); ci += ldc-gvl*0;
            vfloat32m2_t c5 = __riscv_vle32_v_f32m2( &C[ci], gvl); ci += ldc-gvl*0;
            vfloat32m2_t c6 = __riscv_vle32_v_f32m2( &C[ci], gvl); ci += ldc-gvl*0;
            vfloat32m2_t c7 = __riscv_vle32_v_f32m2( &C[ci], gvl); ci += ldc-gvl*0;

            c0 = __riscv_vfmacc_vf_f32m2(c0, alpha, result0, gvl);
            c1 = __riscv_vfmacc_vf_f32m2(c1, alpha, result1, gvl);
            c2 = __riscv_vfmacc_vf_f32m2(c2, alpha, result2, gvl);
            c3 = __riscv_vfmacc_vf_f32m2(c3, alpha, result3, gvl);
            c4 = __riscv_vfmacc_vf_f32m2(c4, alpha, result4, gvl);
            c5 = __riscv_vfmacc_vf_f32m2(c5, alpha, result5, gvl);
            c6 = __riscv_vfmacc_vf_f32m2(c6, alpha, result6, gvl);
            c7 = __riscv_vfmacc_vf_f32m2(c7, alpha, result7, gvl);

            ci = n_top * ldc + m_top;

            __riscv_vse32_v_f32m2( &C[ci], c0, gvl); ci += ldc-gvl*0;
            __riscv_vse32_v_f32m2( &C[ci], c1, gvl); ci += ldc-gvl*0;
            __riscv_vse32_v_f32m2( &C[ci], c2, gvl); ci += ldc-gvl*0;
            __riscv_vse32_v_f32m2( &C[ci], c3, gvl); ci += ldc-gvl*0;
            __riscv_vse32_v_f32m2( &C[ci], c4, gvl); ci += ldc-gvl*0;
            __riscv_vse32_v_f32m2( &C[ci], c5, gvl); ci += ldc-gvl*0;
            __riscv_vse32_v_f32m2( &C[ci], c6, gvl); ci += ldc-gvl*0;
            __riscv_vse32_v_f32m2( &C[ci], c7, gvl); ci += ldc-gvl*0;
            m_top += 8;
        }

        // -- tails for main pass --

        if ( M & 4 ) {
            gvl = __riscv_vsetvl_e16m1(4);

#ifdef BF16_WIDEN_ONE
            bi2 = 0;
#else
            BLASLONG ai=m_top*K;
            BLASLONG bi=n_top*K;
#endif

            vfloat32m1_t result0 = __riscv_vfmv_v_f_f32m1(0.0f, gvl);
            vfloat32m1_t result1 = __riscv_vfmv_v_f_f32m1(0.0f, gvl);
            vfloat32m1_t result2 = __riscv_vfmv_v_f_f32m1(0.0f, gvl);
            vfloat32m1_t result3 = __riscv_vfmv_v_f_f32m1(0.0f, gvl);
            vfloat32m1_t result4 = __riscv_vfmv_v_f_f32m1(0.0f, gvl);
            vfloat32m1_t result5 = __riscv_vfmv_v_f_f32m1(0.0f, gvl);
            vfloat32m1_t result6 = __riscv_vfmv_v_f_f32m1(0.0f, gvl);
            vfloat32m1_t result7 = __riscv_vfmv_v_f_f32m1(0.0f, gvl);

            for (BLASLONG k=0; k < K; ++k) {
#ifdef BF16_WIDEN_ONE
                float B0 = CONV[bi2+0];
                float B1 = CONV[bi2+1];
                float B2 = CONV[bi2+2];
                float B3 = CONV[bi2+3];
                float B4 = CONV[bi2+4];
                float B5 = CONV[bi2+5];
                float B6 = CONV[bi2+6];
                float B7 = CONV[bi2+7];
                bi2 += 8;

                vfloat32m1_t A0 = __riscv_vle32_v_f32m1(&CONV[ai2], gvl);
                ai2 += 4;

                result0 = __riscv_vfmacc_vf_f32m1(result0, B0, A0, gvl);
                result1 = __riscv_vfmacc_vf_f32m1(result1, B1, A0, gvl);
                result2 = __riscv_vfmacc_vf_f32m1(result2, B2, A0, gvl);
                result3 = __riscv_vfmacc_vf_f32m1(result3, B3, A0, gvl);
                result4 = __riscv_vfmacc_vf_f32m1(result4, B4, A0, gvl);
                result5 = __riscv_vfmacc_vf_f32m1(result5, B5, A0, gvl);
                result6 = __riscv_vfmacc_vf_f32m1(result6, B6, A0, gvl);
                result7 = __riscv_vfmacc_vf_f32m1(result7, B7, A0, gvl);
#else
                __bf16 B0 = BB[bi+0];
                __bf16 B1 = BB[bi+1];
                __bf16 B2 = BB[bi+2];
                __bf16 B3 = BB[bi+3];
                __bf16 B4 = BB[bi+4];
                __bf16 B5 = BB[bi+5];
                __bf16 B6 = BB[bi+6];
                __bf16 B7 = BB[bi+7];
                bi += 8;

                vbfloat16mf2_t A0 = __riscv_vle16_v_bf16mf2(&AA[ai + 0 * gvl], gvl);
                ai += 4;

                result0 = __riscv_vfwmaccbf16_vf_f32m1(result0, B0, A0, gvl);
                result1 = __riscv_vfwmaccbf16_vf_f32m1(result1, B1, A0, gvl);
                result2 = __riscv_vfwmaccbf16_vf_f32m1(result2, B2, A0, gvl);
                result3 = __riscv_vfwmaccbf16_vf_f32m1(result3, B3, A0, gvl);
                result4 = __riscv_vfwmaccbf16_vf_f32m1(result4, B4, A0, gvl);
                result5 = __riscv_vfwmaccbf16_vf_f32m1(result5, B5, A0, gvl);
                result6 = __riscv_vfwmaccbf16_vf_f32m1(result6, B6, A0, gvl);
                result7 = __riscv_vfwmaccbf16_vf_f32m1(result7, B7, A0, gvl);
#endif
            }

            BLASLONG ci = n_top * ldc + m_top;

            vfloat32m1_t c0 = __riscv_vle32_v_f32m1(&C[ci], gvl);
            ci += ldc - gvl * 0;
            vfloat32m1_t c1 = __riscv_vle32_v_f32m1(&C[ci], gvl);
            ci += ldc - gvl * 0;
            vfloat32m1_t c2 = __riscv_vle32_v_f32m1(&C[ci], gvl);
            ci += ldc - gvl * 0;
            vfloat32m1_t c3 = __riscv_vle32_v_f32m1(&C[ci], gvl);
            ci += ldc - gvl * 0;
            vfloat32m1_t c4 = __riscv_vle32_v_f32m1(&C[ci], gvl);
            ci += ldc - gvl * 0;
            vfloat32m1_t c5 = __riscv_vle32_v_f32m1(&C[ci], gvl);
            ci += ldc - gvl * 0;
            vfloat32m1_t c6 = __riscv_vle32_v_f32m1(&C[ci], gvl);
            ci += ldc - gvl * 0;
            vfloat32m1_t c7 = __riscv_vle32_v_f32m1(&C[ci], gvl);
            c0 = __riscv_vfmacc_vf_f32m1(c0, alpha, result0, gvl);
            c1 = __riscv_vfmacc_vf_f32m1(c1, alpha, result1, gvl);
            c2 = __riscv_vfmacc_vf_f32m1(c2, alpha, result2, gvl);
            c3 = __riscv_vfmacc_vf_f32m1(c3, alpha, result3, gvl);
            c4 = __riscv_vfmacc_vf_f32m1(c4, alpha, result4, gvl);
            c5 = __riscv_vfmacc_vf_f32m1(c5, alpha, result5, gvl);
            c6 = __riscv_vfmacc_vf_f32m1(c6, alpha, result6, gvl);
            c7 = __riscv_vfmacc_vf_f32m1(c7, alpha, result7, gvl);

            ci= n_top * ldc + m_top;

            __riscv_vse32_v_f32m1(&C[ci], c0, gvl); ci += ldc - gvl * 0;
            __riscv_vse32_v_f32m1(&C[ci], c1, gvl); ci += ldc - gvl * 0;
            __riscv_vse32_v_f32m1(&C[ci], c2, gvl); ci += ldc - gvl * 0;
            __riscv_vse32_v_f32m1(&C[ci], c3, gvl); ci += ldc - gvl * 0;
            __riscv_vse32_v_f32m1(&C[ci], c4, gvl); ci += ldc - gvl * 0;
            __riscv_vse32_v_f32m1(&C[ci], c5, gvl); ci += ldc - gvl * 0;
            __riscv_vse32_v_f32m1(&C[ci], c6, gvl); ci += ldc - gvl * 0;
            __riscv_vse32_v_f32m1(&C[ci], c7, gvl);

            m_top += 4;
        }

        if ( M & 2 ) {

#ifdef BF16_WIDEN_ONE
            bi2 = 0;
#else
            BLASLONG ai=m_top*K;
            BLASLONG bi=n_top*K;
#endif

            float result0 = 0;
            float result1 = 0;
            float result2 = 0;
            float result3 = 0;
            float result4 = 0;
            float result5 = 0;
            float result6 = 0;
            float result7 = 0;
            float result8 = 0;
            float result9 = 0;
            float result10 = 0;
            float result11 = 0;
            float result12 = 0;
            float result13 = 0;
            float result14 = 0;
            float result15 = 0;

            for (BLASLONG k=0; k<K; k++) {
#ifdef BF16_WIDEN_ONE
                result0+=CONV[ai2+0]*CONV[bi2+0];
                result1+=CONV[ai2+1]*CONV[bi2+0];
                result2+=CONV[ai2+0]*CONV[bi2+1];
                result3+=CONV[ai2+1]*CONV[bi2+1];
                result4+=CONV[ai2+0]*CONV[bi2+2];
                result5+=CONV[ai2+1]*CONV[bi2+2];
                result6+=CONV[ai2+0]*CONV[bi2+3];
                result7+=CONV[ai2+1]*CONV[bi2+3];
                result8+=CONV[ai2+0]*CONV[bi2+4];
                result9+=CONV[ai2+1]*CONV[bi2+4];
                result10+=CONV[ai2+0]*CONV[bi2+5];
                result11+=CONV[ai2+1]*CONV[bi2+5];
                result12+=CONV[ai2+0]*CONV[bi2+6];
                result13+=CONV[ai2+1]*CONV[bi2+6];
                result14+=CONV[ai2+0]*CONV[bi2+7];
                result15+=CONV[ai2+1]*CONV[bi2+7];
                ai2+=2;
                bi2+=8;
#else
                result0+=(float)(AA[ai+0])*(float)(BB[bi+0]);
                result1+=(float)(AA[ai+1])*(float)(BB[bi+0]);
                result2+=(float)(AA[ai+0])*(float)(BB[bi+1]);
                result3+=(float)(AA[ai+1])*(float)(BB[bi+1]);
                result4+=(float)(AA[ai+0])*(float)(BB[bi+2]);
                result5+=(float)(AA[ai+1])*(float)(BB[bi+2]);
                result6+=(float)(AA[ai+0])*(float)(BB[bi+3]);
                result7+=(float)(AA[ai+1])*(float)(BB[bi+3]);
                result8+=(float)(AA[ai+0])*(float)(BB[bi+4]);
                result9+=(float)(AA[ai+1])*(float)(BB[bi+4]);
                result10+=(float)(AA[ai+0])*(float)(BB[bi+5]);
                result11+=(float)(AA[ai+1])*(float)(BB[bi+5]);
                result12+=(float)(AA[ai+0])*(float)(BB[bi+6]);
                result13+=(float)(AA[ai+1])*(float)(BB[bi+6]);
                result14+=(float)(AA[ai+0])*(float)(BB[bi+7]);
                result15+=(float)(AA[ai+1])*(float)(BB[bi+7]);
                ai+=2;
                bi+=8;
#endif
            }

            BLASLONG ci=n_top*ldc+m_top;
            C[ci + 0 * ldc + 0] += alpha * result0;
            C[ci + 0 * ldc + 1] += alpha * result1;
            C[ci + 1 * ldc + 0] += alpha * result2;
            C[ci + 1 * ldc + 1] += alpha * result3;
            C[ci + 2 * ldc + 0] += alpha * result4;
            C[ci + 2 * ldc + 1] += alpha * result5;
            C[ci + 3 * ldc + 0] += alpha * result6;
            C[ci + 3 * ldc + 1] += alpha * result7;
            C[ci + 4 * ldc + 0] += alpha * result8;
            C[ci + 4 * ldc + 1] += alpha * result9;
            C[ci + 5 * ldc + 0] += alpha * result10;
            C[ci + 5 * ldc + 1] += alpha * result11;
            C[ci + 6 * ldc + 0] += alpha * result12;
            C[ci + 6 * ldc + 1] += alpha * result13;
            C[ci + 7 * ldc + 0] += alpha * result14;
            C[ci + 7 * ldc + 1] += alpha * result15;

            m_top+=2;
        }

        if ( M & 1 ) {

            float result0 = 0;
            float result1 = 0;
            float result2 = 0;
            float result3 = 0;
            float result4 = 0;
            float result5 = 0;
            float result6 = 0;
            float result7 = 0;

#ifdef BF16_WIDEN_ONE
            bi2 = 0;
#else
            BLASLONG ai=m_top*K;
            BLASLONG bi=n_top*K;
#endif

            for (BLASLONG k=0; k<K; k++) {
#ifdef BF16_WIDEN_ONE
                result0+=CONV[ai2+0]*CONV[bi2+0];
                result1+=CONV[ai2+0]*CONV[bi2+1];
                result2+=CONV[ai2+0]*CONV[bi2+2];
                result3+=CONV[ai2+0]*CONV[bi2+3];
                result4+=CONV[ai2+0]*CONV[bi2+4];
                result5+=CONV[ai2+0]*CONV[bi2+5];
                result6+=CONV[ai2+0]*CONV[bi2+6];
                result7+=CONV[ai2+0]*CONV[bi2+7];
                ai2+=1;
                bi2+=8;
#else
                result0+=(float)(AA[ai+0])*(float)(BB[bi+0]);
                result1+=(float)(AA[ai+0])*(float)(BB[bi+1]);
                result2+=(float)(AA[ai+0])*(float)(BB[bi+2]);
                result3+=(float)(AA[ai+0])*(float)(BB[bi+3]);
                result4+=(float)(AA[ai+0])*(float)(BB[bi+4]);
                result5+=(float)(AA[ai+0])*(float)(BB[bi+5]);
                result6+=(float)(AA[ai+0])*(float)(BB[bi+6]);
                result7+=(float)(AA[ai+0])*(float)(BB[bi+7]);
                ai+=1;
                bi+=8;
#endif
            }

            BLASLONG ci = n_top * ldc + m_top;
            C[ci + 0 * ldc + 0] += alpha * result0;
            C[ci + 1 * ldc + 0] += alpha * result1;
            C[ci + 2 * ldc + 0] += alpha * result2;
            C[ci + 3 * ldc + 0] += alpha * result3;
            C[ci + 4 * ldc + 0] += alpha * result4;
            C[ci + 5 * ldc + 0] += alpha * result5;
            C[ci + 6 * ldc + 0] += alpha * result6;
            C[ci + 7 * ldc + 0] += alpha * result7;
            m_top+=1;
        }

        n_top += 8;
    }

    // -- tails for N=4
    if ( N & 4 ) {
        gvl = __riscv_vsetvl_e16m1(8);
        m_top = 0;

#ifdef BF16_WIDEN_ONE
        BLASLONG bi2 = K * 4;
#ifndef BF16_DONT_CONV
        B_CONV(BB + (n_top*K), CONV, bi2);
#endif
        BLASLONG ai2 = K * 8;
#endif

        for (BLASLONG i=0; i<M/8; i+=1) {
#ifdef BF16_WIDEN_ONE
            bi2 = 0;
#else
            BLASLONG ai=m_top*K;
            BLASLONG bi=n_top*K;
#endif

            vfloat32m2_t result0 = __riscv_vfmv_v_f_f32m2(0.0f, gvl);
            vfloat32m2_t result1 = __riscv_vfmv_v_f_f32m2(0.0f, gvl);
            vfloat32m2_t result2 = __riscv_vfmv_v_f_f32m2(0.0f, gvl);
            vfloat32m2_t result3 = __riscv_vfmv_v_f_f32m2(0.0f, gvl);

            for (BLASLONG k=0; k<K; k++) {
#ifdef BF16_WIDEN_ONE
                float B0 = CONV[bi2+0];
                float B1 = CONV[bi2+1];
                float B2 = CONV[bi2+2];
                float B3 = CONV[bi2+3];
                bi2 += 4;

                vfloat32m2_t A0 = __riscv_vle32_v_f32m2(&CONV[ai2], gvl);
                ai2 += 8;

                result0 = __riscv_vfmacc_vf_f32m2(result0, B0, A0, gvl);
                result1 = __riscv_vfmacc_vf_f32m2(result1, B1, A0, gvl);
                result2 = __riscv_vfmacc_vf_f32m2(result2, B2, A0, gvl);
                result3 = __riscv_vfmacc_vf_f32m2(result3, B3, A0, gvl);
#else
                __bf16 B0 = BB[bi+0];
                __bf16 B1 = BB[bi+1];
                __bf16 B2 = BB[bi+2];
                __bf16 B3 = BB[bi+3];
                bi += 4;

                vbfloat16m1_t A0 = __riscv_vle16_v_bf16m1( &AA[ai+0*gvl], gvl );
                ai += 8;

                result0 = __riscv_vfwmaccbf16_vf_f32m2(result0, B0, A0, gvl);
                result1 = __riscv_vfwmaccbf16_vf_f32m2(result1, B1, A0, gvl);
                result2 = __riscv_vfwmaccbf16_vf_f32m2(result2, B2, A0, gvl);
                result3 = __riscv_vfwmaccbf16_vf_f32m2(result3, B3, A0, gvl);
#endif
            }

            BLASLONG ci=n_top*ldc+m_top;

            vfloat32m2_t c0 = __riscv_vle32_v_f32m2( &C[ci], gvl); ci += ldc - gvl * 0;
            vfloat32m2_t c1 = __riscv_vle32_v_f32m2( &C[ci], gvl); ci += ldc - gvl * 0;
            vfloat32m2_t c2 = __riscv_vle32_v_f32m2( &C[ci], gvl); ci += ldc - gvl * 0;
            vfloat32m2_t c3 = __riscv_vle32_v_f32m2( &C[ci], gvl);

            c0 = __riscv_vfmacc_vf_f32m2(c0, alpha, result0, gvl);
            c1 = __riscv_vfmacc_vf_f32m2(c1, alpha, result1, gvl);
            c2 = __riscv_vfmacc_vf_f32m2(c2, alpha, result2, gvl);
            c3 = __riscv_vfmacc_vf_f32m2(c3, alpha, result3, gvl);

            ci = n_top * ldc + m_top;

            __riscv_vse32_v_f32m2( &C[ci], c0, gvl); ci += ldc-gvl*0;
            __riscv_vse32_v_f32m2( &C[ci], c1, gvl); ci += ldc-gvl*0;
            __riscv_vse32_v_f32m2( &C[ci], c2, gvl); ci += ldc-gvl*0;
            __riscv_vse32_v_f32m2( &C[ci], c3, gvl);
            m_top += 8;
        }

        if ( M & 4 ) {
            gvl = __riscv_vsetvl_e16m1(4);

#ifdef BF16_WIDEN_ONE
            bi2 = 0;
#else
            BLASLONG ai=m_top*K;
            BLASLONG bi=n_top*K;
#endif

            vfloat32m1_t result0 = __riscv_vfmv_v_f_f32m1(0.0f, gvl);
            vfloat32m1_t result1 = __riscv_vfmv_v_f_f32m1(0.0f, gvl);
            vfloat32m1_t result2 = __riscv_vfmv_v_f_f32m1(0.0f, gvl);
            vfloat32m1_t result3 = __riscv_vfmv_v_f_f32m1(0.0f, gvl);

            for (BLASLONG k=0; k < K; ++k) {
#ifdef BF16_WIDEN_ONE
                float B0 = CONV[bi2+0];
                float B1 = CONV[bi2+1];
                float B2 = CONV[bi2+2];
                float B3 = CONV[bi2+3];
                bi2 += 4;

                vfloat32m1_t A0 = __riscv_vle32_v_f32m1(&CONV[ai2], gvl);
                ai2 += 4;

                result0 = __riscv_vfmacc_vf_f32m1(result0, B0, A0, gvl);
                result1 = __riscv_vfmacc_vf_f32m1(result1, B1, A0, gvl);
                result2 = __riscv_vfmacc_vf_f32m1(result2, B2, A0, gvl);
                result3 = __riscv_vfmacc_vf_f32m1(result3, B3, A0, gvl);
#else
                __bf16 B0 = BB[bi+0];
                __bf16 B1 = BB[bi+1];
                __bf16 B2 = BB[bi+2];
                __bf16 B3 = BB[bi+3];
                bi += 4;

                vbfloat16mf2_t A0 = __riscv_vle16_v_bf16mf2(&AA[ai + 0 * gvl], gvl);
                ai += 4;

                result0 = __riscv_vfwmaccbf16_vf_f32m1(result0, B0, A0, gvl);
                result1 = __riscv_vfwmaccbf16_vf_f32m1(result1, B1, A0, gvl);
                result2 = __riscv_vfwmaccbf16_vf_f32m1(result2, B2, A0, gvl);
                result3 = __riscv_vfwmaccbf16_vf_f32m1(result3, B3, A0, gvl);
#endif
            }

            BLASLONG ci = n_top * ldc + m_top;

            vfloat32m1_t c0 = __riscv_vle32_v_f32m1(&C[ci], gvl);
            ci += ldc - gvl * 0;
            vfloat32m1_t c1 = __riscv_vle32_v_f32m1(&C[ci], gvl);
            ci += ldc - gvl * 0;
            vfloat32m1_t c2 = __riscv_vle32_v_f32m1(&C[ci], gvl);
            ci += ldc - gvl * 0;
            vfloat32m1_t c3 = __riscv_vle32_v_f32m1(&C[ci], gvl);
            c0 = __riscv_vfmacc_vf_f32m1(c0, alpha, result0, gvl);
            c1 = __riscv_vfmacc_vf_f32m1(c1, alpha, result1, gvl);
            c2 = __riscv_vfmacc_vf_f32m1(c2, alpha, result2, gvl);
            c3 = __riscv_vfmacc_vf_f32m1(c3, alpha, result3, gvl);

            ci= n_top * ldc + m_top;

            __riscv_vse32_v_f32m1(&C[ci], c0, gvl); ci += ldc - gvl * 0;
            __riscv_vse32_v_f32m1(&C[ci], c1, gvl); ci += ldc - gvl * 0;
            __riscv_vse32_v_f32m1(&C[ci], c2, gvl); ci += ldc - gvl * 0;
            __riscv_vse32_v_f32m1(&C[ci], c3, gvl);

            m_top += 4;
        }

        if ( M & 2 ) {

#ifdef BF16_WIDEN_ONE
            bi2 = 0;
#else
            BLASLONG ai=m_top*K;
            BLASLONG bi=n_top*K;
#endif

            float result0 = 0;
            float result1 = 0;
            float result2 = 0;
            float result3 = 0;
            float result4 = 0;
            float result5 = 0;
            float result6 = 0;
            float result7 = 0;

            for (BLASLONG k=0; k<K; k++) {
#ifdef BF16_WIDEN_ONE
                result0+=CONV[ai2+0]*CONV[bi2+0];
                result1+=CONV[ai2+1]*CONV[bi2+0];
                result2+=CONV[ai2+0]*CONV[bi2+1];
                result3+=CONV[ai2+1]*CONV[bi2+1];
                result4+=CONV[ai2+0]*CONV[bi2+2];
                result5+=CONV[ai2+1]*CONV[bi2+2];
                result6+=CONV[ai2+0]*CONV[bi2+3];
                result7+=CONV[ai2+1]*CONV[bi2+3];
                ai2+=2;
                bi2+=4;
#else
                result0+=(float)(AA[ai+0])*(float)(BB[bi+0]);
                result1+=(float)(AA[ai+1])*(float)(BB[bi+0]);
                result2+=(float)(AA[ai+0])*(float)(BB[bi+1]);
                result3+=(float)(AA[ai+1])*(float)(BB[bi+1]);
                result4+=(float)(AA[ai+0])*(float)(BB[bi+2]);
                result5+=(float)(AA[ai+1])*(float)(BB[bi+2]);
                result6+=(float)(AA[ai+0])*(float)(BB[bi+3]);
                result7+=(float)(AA[ai+1])*(float)(BB[bi+3]);
                ai+=2;
                bi+=4;
#endif
            }

            BLASLONG ci=n_top*ldc+m_top;
            C[ci + 0 * ldc + 0] += alpha * result0;
            C[ci + 0 * ldc + 1] += alpha * result1;
            C[ci + 1 * ldc + 0] += alpha * result2;
            C[ci + 1 * ldc + 1] += alpha * result3;
            C[ci + 2 * ldc + 0] += alpha * result4;
            C[ci + 2 * ldc + 1] += alpha * result5;
            C[ci + 3 * ldc + 0] += alpha * result6;
            C[ci + 3 * ldc + 1] += alpha * result7;

            m_top += 2;
        }

        if ( M & 1 ) {

            float result0 = 0;
            float result1 = 0;
            float result2 = 0;
            float result3 = 0;

#ifdef BF16_WIDEN_ONE
            bi2 = 0;
#else
            BLASLONG ai=m_top*K;
            BLASLONG bi=n_top*K;
#endif

            for (BLASLONG k=0; k<K; k++) {
#ifdef BF16_WIDEN_ONE
                result0+=CONV[ai2+0]*CONV[bi2+0];
                result1+=CONV[ai2+0]*CONV[bi2+1];
                result2+=CONV[ai2+0]*CONV[bi2+2];
                result3+=CONV[ai2+0]*CONV[bi2+3];
                ai2+=1;
                bi2+=4;
#else
                result0+=(float)(AA[ai+0])*(float)(BB[bi+0]);
                result1+=(float)(AA[ai+0])*(float)(BB[bi+1]);
                result2+=(float)(AA[ai+0])*(float)(BB[bi+2]);
                result3+=(float)(AA[ai+0])*(float)(BB[bi+3]);
                ai+=1;
                bi+=4;
#endif
            }

            BLASLONG ci = n_top * ldc + m_top;
            C[ci + 0 * ldc + 0] += alpha * result0;
            C[ci + 1 * ldc + 0] += alpha * result1;
            C[ci + 2 * ldc + 0] += alpha * result2;
            C[ci + 3 * ldc + 0] += alpha * result3;
            m_top += 1;
        }

        n_top += 4;
    }

    // -- tails for N=2
    if ( N & 2 ) {
        gvl = __riscv_vsetvl_e16m1(8);
        m_top = 0;
#ifdef BF16_WIDEN_ONE
        BLASLONG bi2 = K * 2;
#ifndef BF16_DONT_CONV
        B_CONV(BB + (n_top*K), CONV, bi2);
#endif
        BLASLONG ai2 = K * 8;
#endif

        for (BLASLONG i=0; i<M/8; i+=1) {
#ifdef BF16_WIDEN_ONE
            bi2 = 0;
#else
            BLASLONG ai=m_top*K;
            BLASLONG bi=n_top*K;
#endif

            vfloat32m2_t result0 = __riscv_vfmv_v_f_f32m2(0.0f, gvl);
            vfloat32m2_t result1 = __riscv_vfmv_v_f_f32m2(0.0f, gvl);

            for (BLASLONG k=0; k<K; k++) {
#ifdef BF16_WIDEN_ONE
                float B0 = CONV[bi2+0];
                float B1 = CONV[bi2+1];
                bi2 += 2;

                vfloat32m2_t A0 = __riscv_vle32_v_f32m2(&CONV[ai2], gvl);
                ai2 += 8;

                result0 = __riscv_vfmacc_vf_f32m2(result0, B0, A0, gvl);
                result1 = __riscv_vfmacc_vf_f32m2(result1, B1, A0, gvl);
#else
                __bf16 B0 = BB[bi+0];
                __bf16 B1 = BB[bi+1];
                bi += 2;

                vbfloat16m1_t A0 = __riscv_vle16_v_bf16m1( &AA[ai+0*gvl], gvl );
                ai += 8;

                result0 = __riscv_vfwmaccbf16_vf_f32m2(result0, B0, A0, gvl);
                result1 = __riscv_vfwmaccbf16_vf_f32m2(result1, B1, A0, gvl);
#endif
            }

            BLASLONG ci=n_top*ldc+m_top;

            vfloat32m2_t c0 = __riscv_vle32_v_f32m2( &C[ci], gvl); ci += ldc - gvl * 0;
            vfloat32m2_t c1 = __riscv_vle32_v_f32m2( &C[ci], gvl);

            c0 = __riscv_vfmacc_vf_f32m2(c0, alpha, result0, gvl);
            c1 = __riscv_vfmacc_vf_f32m2(c1, alpha, result1, gvl);

            ci = n_top * ldc + m_top;

            __riscv_vse32_v_f32m2( &C[ci], c0, gvl); ci += ldc-gvl*0;
            __riscv_vse32_v_f32m2( &C[ci], c1, gvl);
            m_top += 8;
        }

        if ( M & 4 ) {
            gvl = __riscv_vsetvl_e16m1(4);

#ifdef BF16_WIDEN_ONE
            bi2 = 0;
#else
            BLASLONG ai=m_top*K;
            BLASLONG bi=n_top*K;
#endif

            vfloat32m1_t result0 = __riscv_vfmv_v_f_f32m1(0.0f, gvl);
            vfloat32m1_t result1 = __riscv_vfmv_v_f_f32m1(0.0f, gvl);

            for (BLASLONG k=0; k < K; ++k) {
#ifdef BF16_WIDEN_ONE
                float B0 = CONV[bi2+0];
                float B1 = CONV[bi2+1];
                bi2 += 2;

                vfloat32m1_t A0 = __riscv_vle32_v_f32m1(&CONV[ai2], gvl);
                ai2 += 4;

                result0 = __riscv_vfmacc_vf_f32m1(result0, B0, A0, gvl);
                result1 = __riscv_vfmacc_vf_f32m1(result1, B1, A0, gvl);
#else
                __bf16 B0 = BB[bi+0];
                __bf16 B1 = BB[bi+1];
                bi += 2;

                vbfloat16mf2_t A0 = __riscv_vle16_v_bf16mf2(&AA[ai + 0 * gvl], gvl);
                ai += 4;

                result0 = __riscv_vfwmaccbf16_vf_f32m1(result0, B0, A0, gvl);
                result1 = __riscv_vfwmaccbf16_vf_f32m1(result1, B1, A0, gvl);
#endif
            }

            BLASLONG ci = n_top * ldc + m_top;

            vfloat32m1_t c0 = __riscv_vle32_v_f32m1(&C[ci], gvl);
            ci += ldc - gvl * 0;
            vfloat32m1_t c1 = __riscv_vle32_v_f32m1(&C[ci], gvl);
            c0 = __riscv_vfmacc_vf_f32m1(c0, alpha, result0, gvl);
            c1 = __riscv_vfmacc_vf_f32m1(c1, alpha, result1, gvl);

            ci= n_top * ldc + m_top;

            __riscv_vse32_v_f32m1(&C[ci], c0, gvl); ci += ldc - gvl * 0;
            __riscv_vse32_v_f32m1(&C[ci], c1, gvl);

            m_top += 4;
        }

        if ( M & 2 ) {

#ifdef BF16_WIDEN_ONE
            bi2 = 0;
#else
            BLASLONG ai=m_top*K;
            BLASLONG bi=n_top*K;
#endif

            float result0 = 0;
            float result1 = 0;
            float result2 = 0;
            float result3 = 0;

            for (BLASLONG k=0; k<K; k++) {
#ifdef BF16_WIDEN_ONE
                result0+=CONV[ai2+0]*CONV[bi2+0];
                result1+=CONV[ai2+1]*CONV[bi2+0];
                result2+=CONV[ai2+0]*CONV[bi2+1];
                result3+=CONV[ai2+1]*CONV[bi2+1];
                ai2+=2;
                bi2+=2;
#else
                result0+=(float)(AA[ai+0])*(float)(BB[bi+0]);
                result1+=(float)(AA[ai+1])*(float)(BB[bi+0]);
                result2+=(float)(AA[ai+0])*(float)(BB[bi+1]);
                result3+=(float)(AA[ai+1])*(float)(BB[bi+1]);
                ai+=2;
                bi+=2;
#endif
            }

            BLASLONG ci=n_top*ldc+m_top;
            C[ci + 0 * ldc + 0] += alpha * result0;
            C[ci + 0 * ldc + 1] += alpha * result1;
            C[ci + 1 * ldc + 0] += alpha * result2;
            C[ci + 1 * ldc + 1] += alpha * result3;

            m_top += 2;
        }


        if ( M & 1 ) {

            float result0 = 0;
            float result1 = 0;

#ifdef BF16_WIDEN_ONE
            bi2 = 0;
#else
            BLASLONG ai=m_top*K;
            BLASLONG bi=n_top*K;
#endif

            for (BLASLONG k=0; k<K; k++) {
#ifdef BF16_WIDEN_ONE
                result0+=CONV[ai2+0]*CONV[bi2+0];
                result1+=CONV[ai2+0]*CONV[bi2+1];
                ai2+=1;
                bi2+=2;
#else
                result0+=(float)(AA[ai+0])*(float)(BB[bi+0]);
                result1+=(float)(AA[ai+0])*(float)(BB[bi+1]);
                ai+=1;
                bi+=2;
#endif
            }

            BLASLONG ci = n_top * ldc + m_top;
            C[ci + 0 * ldc + 0] += alpha * result0;
            C[ci + 1 * ldc + 0] += alpha * result1;
            m_top += 1;
        }

        n_top += 2;
    }

    // -- tails for N=1
    if ( N & 1 ) {
        gvl = __riscv_vsetvl_e16m1(8);
        m_top = 0;
#ifdef BF16_WIDEN_ONE
        BLASLONG bi2 = K * 1;
#ifndef BF16_DONT_CONV
        B_CONV(BB + (n_top*K), CONV, bi2);
#endif
        BLASLONG ai2 = K * 8;
#endif

        for (BLASLONG i=0; i<M/8; i+=1) {
#ifdef BF16_WIDEN_ONE
            bi2 = 0;
#else
            BLASLONG ai=m_top*K;
            BLASLONG bi=n_top*K;
#endif

            vfloat32m2_t result0 = __riscv_vfmv_v_f_f32m2(0.0f, gvl);

            for (BLASLONG k=0; k<K; k++) {
#ifdef BF16_WIDEN_ONE
                float B0 = CONV[bi2+0];
                bi2 += 1;

                vfloat32m2_t A0 = __riscv_vle32_v_f32m2(&CONV[ai2], gvl);
                ai2 += 8;

                result0 = __riscv_vfmacc_vf_f32m2(result0, B0, A0, gvl);
#else
                __bf16 B0 = BB[bi+0];
                bi += 1;

                vbfloat16mf2_t A0 = __riscv_vle16_v_bf16mf2( &AA[ai+0*gvl], gvl );
                ai += 8;

                result0 = __riscv_vfwmaccbf16_vf_f32m1(result0, B0, A0, gvl);
#endif
            }

            BLASLONG ci=n_top*ldc+m_top;

            vfloat32m2_t c0 = __riscv_vle32_v_f32m2( &C[ci], gvl);

            c0 = __riscv_vfmacc_vf_f32m2(c0, alpha, result0, gvl);

            ci = n_top * ldc + m_top;

            __riscv_vse32_v_f32m2( &C[ci], c0, gvl);
            m_top += 8;
        }

        if ( M & 4 ) {
            gvl = __riscv_vsetvl_e16m1(4);

#ifdef BF16_WIDEN_ONE
            bi2 = 0;
#else
            BLASLONG ai=m_top*K;
            BLASLONG bi=n_top*K;
#endif

            vfloat32m1_t result0 = __riscv_vfmv_v_f_f32m1(0.0f, gvl);

            for (BLASLONG k=0; k < K; ++k) {
#ifdef BF16_WIDEN_ONE
                float B0 = CONV[bi2+0];
                bi2 += 1;

                vfloat32m1_t A0 = __riscv_vle32_v_f32m1(&CONV[ai2], gvl);
                ai2 += 4;

                result0 = __riscv_vfmacc_vf_f32m1(result0, B0, A0, gvl);
#else
                __bf16 B0 = BB[bi+0];
                bi += 1;

                vbfloat16mf2_t A0 = __riscv_vle16_v_bf16mf2(&AA[ai + 0 * gvl], gvl);
                ai += 4;

                result0 = __riscv_vfwmaccbf16_vf_f32m1(result0, B0, A0, gvl);
#endif
            }

            BLASLONG ci = n_top * ldc + m_top;

            vfloat32m1_t c0 = __riscv_vle32_v_f32m1(&C[ci], gvl);
            c0 = __riscv_vfmacc_vf_f32m1(c0, alpha, result0, gvl);

            ci= n_top * ldc + m_top;

            __riscv_vse32_v_f32m1(&C[ci], c0, gvl);
            m_top += 4;
        }

        if ( M & 2 ) {

#ifdef BF16_WIDEN_ONE
            bi2 = 0;
#else
            BLASLONG ai=m_top*K;
            BLASLONG bi=n_top*K;
#endif

            float result0 = 0;
            float result1 = 0;

            for (BLASLONG k=0; k<K; k++) {
#ifdef BF16_WIDEN_ONE
                result0+=CONV[ai2+0]*CONV[bi2+0];
                result1+=CONV[ai2+1]*CONV[bi2+0];
                ai2+=2;
                bi2+=1;
#else
                result0+=(float)(AA[ai+0])*(float)(BB[bi+0]);
                result1+=(float)(AA[ai+1])*(float)(BB[bi+0]);
                ai+=2;
                bi+=1;
#endif
            }

            BLASLONG ci=n_top*ldc+m_top;
            C[ci + 0 * ldc + 0] += alpha * result0;
            C[ci + 0 * ldc + 1] += alpha * result1;

            m_top += 2;
        }

        if ( M & 1 ) {

            float result0 = 0;

#ifdef BF16_WIDEN_ONE
            bi2 = 0;
#else
            BLASLONG ai=m_top*K;
            BLASLONG bi=n_top*K;
#endif

            for (BLASLONG k=0; k<K; k++) {
#ifdef BF16_WIDEN_ONE
                result0+=CONV[ai2+0]*CONV[bi2+0];
                ai2+=1;
                bi2+=1;
#else
                result0+=(float)(AA[ai+0])*(float)(BB[bi+0]);
                ai+=1;
                bi+=1;
#endif
            }

            BLASLONG ci = n_top * ldc + m_top;
            C[ci + 0 * ldc + 0] += alpha * result0;
            m_top += 1;
        }

        n_top += 1;
    }
#ifdef BF16_WIDEN_ONE
    free(CONV);
#endif
    return 0;
}
