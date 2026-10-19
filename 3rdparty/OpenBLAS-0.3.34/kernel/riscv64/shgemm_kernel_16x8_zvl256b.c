#include "common.h"
#include <riscv_vector.h>

#define FP16_NARROW     // Accumulate in FP16

int CNAME(BLASLONG M, BLASLONG N, BLASLONG K, FLOAT alpha, IFLOAT *A, IFLOAT *B, FLOAT *C, BLASLONG ldc)
{
    BLASLONG gvl = 0;
    BLASLONG m_top = 0;
    BLASLONG n_top = 0;
#ifdef FP16_NARROW
    IFLOAT alpha16 = (IFLOAT)(alpha);
#endif

    // -- MAIN PASS
    for (BLASLONG j=0; j<N/8; j+=1) {
        m_top = 0;
#ifdef FP16_NARROW
        BLASLONG gvl = __riscv_vsetvl_e16m2(32);

        for (BLASLONG i=0; i<M/32; i+=1) {
            BLASLONG ai=m_top*K;
            BLASLONG bi=n_top*K;

            _Float16 B0 = B[bi+0];
            _Float16 B1 = B[bi+1];
            _Float16 B2 = B[bi+2];
            _Float16 B3 = B[bi+3];
            _Float16 B4 = B[bi+4];
            _Float16 B5 = B[bi+5];
            _Float16 B6 = B[bi+6];
            _Float16 B7 = B[bi+7];
            bi += 8;

            vfloat16m1_t A00 = __riscv_vle16_v_f16m1( &A[ai+0*gvl], 16 );
            vfloat16m1_t A01 = __riscv_vle16_v_f16m1( &A[ai+0*gvl+16*K], 16 );
            vfloat16m2_t A0 = __riscv_vcreate_v_f16m1_f16m2(A00, A01);
            ai += 16;

            vfloat16m2_t result0 = __riscv_vfmul_vf_f16m2( A0, B0, gvl);
            vfloat16m2_t result1 = __riscv_vfmul_vf_f16m2( A0, B1, gvl);
            vfloat16m2_t result2 = __riscv_vfmul_vf_f16m2( A0, B2, gvl);
            vfloat16m2_t result3 = __riscv_vfmul_vf_f16m2( A0, B3, gvl);
            vfloat16m2_t result4 = __riscv_vfmul_vf_f16m2( A0, B4, gvl);
            vfloat16m2_t result5 = __riscv_vfmul_vf_f16m2( A0, B5, gvl);
            vfloat16m2_t result6 = __riscv_vfmul_vf_f16m2( A0, B6, gvl);
            vfloat16m2_t result7 = __riscv_vfmul_vf_f16m2( A0, B7, gvl);

            for(BLASLONG k=1; k<K; k++) {
                B0 = B[bi+0];
                B1 = B[bi+1];
                B2 = B[bi+2];
                B3 = B[bi+3];
                B4 = B[bi+4];
                B5 = B[bi+5];
                B6 = B[bi+6];
                B7 = B[bi+7];
                bi += 8;
                A00 = __riscv_vle16_v_f16m1( &A[ai+0*gvl], 16 );
                A01 = __riscv_vle16_v_f16m1( &A[ai+0*gvl+16*K], 16 );
                A0 = __riscv_vcreate_v_f16m1_f16m2(A00, A01);
                ai += 16;

                result0 = __riscv_vfmacc_vf_f16m2(result0, B0, A0, gvl);
                result1 = __riscv_vfmacc_vf_f16m2(result1, B1, A0, gvl);
                result2 = __riscv_vfmacc_vf_f16m2(result2, B2, A0, gvl);
                result3 = __riscv_vfmacc_vf_f16m2(result3, B3, A0, gvl);
                result4 = __riscv_vfmacc_vf_f16m2(result4, B4, A0, gvl);
                result5 = __riscv_vfmacc_vf_f16m2(result5, B5, A0, gvl);
                result6 = __riscv_vfmacc_vf_f16m2(result6, B6, A0, gvl);
                result7 = __riscv_vfmacc_vf_f16m2(result7, B7, A0, gvl);
            }

            BLASLONG ci=n_top*ldc+m_top;

            vfloat32m4_t c0 = __riscv_vle32_v_f32m4( &C[ci], gvl); ci += ldc-gvl*0;
            vfloat32m4_t c1 = __riscv_vle32_v_f32m4( &C[ci], gvl); ci += ldc-gvl*0;
            vfloat32m4_t c2 = __riscv_vle32_v_f32m4( &C[ci], gvl); ci += ldc-gvl*0;
            vfloat32m4_t c3 = __riscv_vle32_v_f32m4( &C[ci], gvl);

            ci-=ldc*3;

            c0 = __riscv_vfwmacc_vf_f32m4(c0, alpha16, result0, gvl);
            c1 = __riscv_vfwmacc_vf_f32m4(c1, alpha16, result1, gvl);
            c2 = __riscv_vfwmacc_vf_f32m4(c2, alpha16, result2, gvl);
            c3 = __riscv_vfwmacc_vf_f32m4(c3, alpha16, result3, gvl);

            __riscv_vse32_v_f32m4( &C[ci], c0, gvl); ci += ldc-gvl*0;
            __riscv_vse32_v_f32m4( &C[ci], c1, gvl); ci += ldc-gvl*0;
            __riscv_vse32_v_f32m4( &C[ci], c2, gvl); ci += ldc-gvl*0;
            __riscv_vse32_v_f32m4( &C[ci], c3, gvl); ci += ldc-gvl*0;

            vfloat32m4_t c4 = __riscv_vle32_v_f32m4( &C[ci], gvl); ci += ldc-gvl*0;
            vfloat32m4_t c5 = __riscv_vle32_v_f32m4( &C[ci], gvl); ci += ldc-gvl*0;
            vfloat32m4_t c6 = __riscv_vle32_v_f32m4( &C[ci], gvl); ci += ldc-gvl*0;
            vfloat32m4_t c7 = __riscv_vle32_v_f32m4( &C[ci], gvl);

            ci-=ldc*3;

            c4 = __riscv_vfwmacc_vf_f32m4(c4, alpha16, result4, gvl);
            c5 = __riscv_vfwmacc_vf_f32m4(c5, alpha16, result5, gvl);
            c6 = __riscv_vfwmacc_vf_f32m4(c6, alpha16, result6, gvl);
            c7 = __riscv_vfwmacc_vf_f32m4(c7, alpha16, result7, gvl);

            __riscv_vse32_v_f32m4( &C[ci], c4, gvl); ci += ldc-gvl*0;
            __riscv_vse32_v_f32m4( &C[ci], c5, gvl); ci += ldc-gvl*0;
            __riscv_vse32_v_f32m4( &C[ci], c6, gvl); ci += ldc-gvl*0;
            __riscv_vse32_v_f32m4( &C[ci], c7, gvl);
            m_top += 32;
        }

        if (M & 16) {
          gvl = __riscv_vsetvl_e16m1(16);
#else
        BLASLONG gvl = __riscv_vsetvl_e16m1(16);

        for (BLASLONG i=0; i<M/16; i+=1) {
#endif
            BLASLONG ai=m_top*K;	
            BLASLONG bi=n_top*K;	

            _Float16 B0 = B[bi+0];
            _Float16 B1 = B[bi+1];
            _Float16 B2 = B[bi+2];
            _Float16 B3 = B[bi+3];
            _Float16 B4 = B[bi+4];
            _Float16 B5 = B[bi+5];
            _Float16 B6 = B[bi+6];
            _Float16 B7 = B[bi+7];
            bi += 8;

            vfloat16m1_t A0 = __riscv_vle16_v_f16m1( &A[ai+0*gvl], gvl );
            ai += 16;

#ifdef FP16_NARROW
            vfloat16m1_t result0 = __riscv_vfmul_vf_f16m1( A0, B0, gvl);
            vfloat16m1_t result1 = __riscv_vfmul_vf_f16m1( A0, B1, gvl);
            vfloat16m1_t result2 = __riscv_vfmul_vf_f16m1( A0, B2, gvl);
            vfloat16m1_t result3 = __riscv_vfmul_vf_f16m1( A0, B3, gvl);
            vfloat16m1_t result4 = __riscv_vfmul_vf_f16m1( A0, B4, gvl);
            vfloat16m1_t result5 = __riscv_vfmul_vf_f16m1( A0, B5, gvl);
            vfloat16m1_t result6 = __riscv_vfmul_vf_f16m1( A0, B6, gvl);
            vfloat16m1_t result7 = __riscv_vfmul_vf_f16m1( A0, B7, gvl);
#else
            vfloat32m2_t result0 = __riscv_vfwmul_vf_f32m2( A0, B0, gvl);
            vfloat32m2_t result1 = __riscv_vfwmul_vf_f32m2( A0, B1, gvl);
            vfloat32m2_t result2 = __riscv_vfwmul_vf_f32m2( A0, B2, gvl);
            vfloat32m2_t result3 = __riscv_vfwmul_vf_f32m2( A0, B3, gvl);
            vfloat32m2_t result4 = __riscv_vfwmul_vf_f32m2( A0, B4, gvl);
            vfloat32m2_t result5 = __riscv_vfwmul_vf_f32m2( A0, B5, gvl);
            vfloat32m2_t result6 = __riscv_vfwmul_vf_f32m2( A0, B6, gvl);
            vfloat32m2_t result7 = __riscv_vfwmul_vf_f32m2( A0, B7, gvl);
#endif
            
            for(BLASLONG k=1; k<K; k++) {
                B0 = B[bi+0];
                B1 = B[bi+1];
                B2 = B[bi+2];
                B3 = B[bi+3];
                B4 = B[bi+4];
                B5 = B[bi+5];
                B6 = B[bi+6];
                B7 = B[bi+7];
                bi += 8;
                A0 = __riscv_vle16_v_f16m1( &A[ai+0*gvl], gvl );
                ai += 16;

#ifdef FP16_NARROW
                result0 = __riscv_vfmacc_vf_f16m1(result0, B0, A0, gvl);
                result1 = __riscv_vfmacc_vf_f16m1(result1, B1, A0, gvl);
                result2 = __riscv_vfmacc_vf_f16m1(result2, B2, A0, gvl);
                result3 = __riscv_vfmacc_vf_f16m1(result3, B3, A0, gvl);
                result4 = __riscv_vfmacc_vf_f16m1(result4, B4, A0, gvl);
                result5 = __riscv_vfmacc_vf_f16m1(result5, B5, A0, gvl);
                result6 = __riscv_vfmacc_vf_f16m1(result6, B6, A0, gvl);
                result7 = __riscv_vfmacc_vf_f16m1(result7, B7, A0, gvl);
#else
                result0 = __riscv_vfwmacc_vf_f32m2(result0, B0, A0, gvl);
                result1 = __riscv_vfwmacc_vf_f32m2(result1, B1, A0, gvl);
                result2 = __riscv_vfwmacc_vf_f32m2(result2, B2, A0, gvl);
                result3 = __riscv_vfwmacc_vf_f32m2(result3, B3, A0, gvl);
                result4 = __riscv_vfwmacc_vf_f32m2(result4, B4, A0, gvl);
                result5 = __riscv_vfwmacc_vf_f32m2(result5, B5, A0, gvl);
                result6 = __riscv_vfwmacc_vf_f32m2(result6, B6, A0, gvl);
                result7 = __riscv_vfwmacc_vf_f32m2(result7, B7, A0, gvl);
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
            vfloat32m2_t c7 = __riscv_vle32_v_f32m2( &C[ci], gvl);

#ifdef FP16_NARROW
            c0 = __riscv_vfwmacc_vf_f32m2(c0, alpha16, result0, gvl);
            c1 = __riscv_vfwmacc_vf_f32m2(c1, alpha16, result1, gvl);
            c2 = __riscv_vfwmacc_vf_f32m2(c2, alpha16, result2, gvl);
            c3 = __riscv_vfwmacc_vf_f32m2(c3, alpha16, result3, gvl);
            c4 = __riscv_vfwmacc_vf_f32m2(c4, alpha16, result4, gvl);
            c5 = __riscv_vfwmacc_vf_f32m2(c5, alpha16, result5, gvl);
            c6 = __riscv_vfwmacc_vf_f32m2(c6, alpha16, result6, gvl);
            c7 = __riscv_vfwmacc_vf_f32m2(c7, alpha16, result7, gvl);
#else
            c0 = __riscv_vfmacc_vf_f32m2(c0, alpha, result0, gvl);
            c1 = __riscv_vfmacc_vf_f32m2(c1, alpha, result1, gvl);
            c2 = __riscv_vfmacc_vf_f32m2(c2, alpha, result2, gvl);
            c3 = __riscv_vfmacc_vf_f32m2(c3, alpha, result3, gvl);
            c4 = __riscv_vfmacc_vf_f32m2(c4, alpha, result4, gvl);
            c5 = __riscv_vfmacc_vf_f32m2(c5, alpha, result5, gvl);
            c6 = __riscv_vfmacc_vf_f32m2(c6, alpha, result6, gvl);
            c7 = __riscv_vfmacc_vf_f32m2(c7, alpha, result7, gvl);
#endif

            ci=n_top*ldc+m_top;

            __riscv_vse32_v_f32m2( &C[ci], c0, gvl); ci += ldc-gvl*0;
            __riscv_vse32_v_f32m2( &C[ci], c1, gvl); ci += ldc-gvl*0;
            __riscv_vse32_v_f32m2( &C[ci], c2, gvl); ci += ldc-gvl*0;
            __riscv_vse32_v_f32m2( &C[ci], c3, gvl); ci += ldc-gvl*0;
            __riscv_vse32_v_f32m2( &C[ci], c4, gvl); ci += ldc-gvl*0;
            __riscv_vse32_v_f32m2( &C[ci], c5, gvl); ci += ldc-gvl*0;
            __riscv_vse32_v_f32m2( &C[ci], c6, gvl); ci += ldc-gvl*0;
            __riscv_vse32_v_f32m2( &C[ci], c7, gvl);
            m_top += 16;
        }



        // -- tails for main pass
        
        if( M & 8 ) {
            gvl = __riscv_vsetvl_e16mf2(8);

            BLASLONG ai=m_top*K;
            BLASLONG bi=n_top*K;
            _Float16 B0 = B[bi+0];
            _Float16 B1 = B[bi+1];
            _Float16 B2 = B[bi+2];
            _Float16 B3 = B[bi+3];
            _Float16 B4 = B[bi+4];
            _Float16 B5 = B[bi+5];
            _Float16 B6 = B[bi+6];
            _Float16 B7 = B[bi+7];
            bi += 8;

            vfloat16mf2_t A0 = __riscv_vle16_v_f16mf2( &A[ai+0*gvl], gvl );
            ai += 8;

#ifdef FP16_NARROW
            vfloat16mf2_t result0 = __riscv_vfmul_vf_f16mf2( A0, B0, gvl);
            vfloat16mf2_t result1 = __riscv_vfmul_vf_f16mf2( A0, B1, gvl);
            vfloat16mf2_t result2 = __riscv_vfmul_vf_f16mf2( A0, B2, gvl);
            vfloat16mf2_t result3 = __riscv_vfmul_vf_f16mf2( A0, B3, gvl);
            vfloat16mf2_t result4 = __riscv_vfmul_vf_f16mf2( A0, B4, gvl);
            vfloat16mf2_t result5 = __riscv_vfmul_vf_f16mf2( A0, B5, gvl);
            vfloat16mf2_t result6 = __riscv_vfmul_vf_f16mf2( A0, B6, gvl);
            vfloat16mf2_t result7 = __riscv_vfmul_vf_f16mf2( A0, B7, gvl);
#else
            vfloat32m1_t result0 = __riscv_vfwmul_vf_f32m1( A0, B0, gvl);
            vfloat32m1_t result1 = __riscv_vfwmul_vf_f32m1( A0, B1, gvl);
            vfloat32m1_t result2 = __riscv_vfwmul_vf_f32m1( A0, B2, gvl);
            vfloat32m1_t result3 = __riscv_vfwmul_vf_f32m1( A0, B3, gvl);
            vfloat32m1_t result4 = __riscv_vfwmul_vf_f32m1( A0, B4, gvl);
            vfloat32m1_t result5 = __riscv_vfwmul_vf_f32m1( A0, B5, gvl);
            vfloat32m1_t result6 = __riscv_vfwmul_vf_f32m1( A0, B6, gvl);
            vfloat32m1_t result7 = __riscv_vfwmul_vf_f32m1( A0, B7, gvl);
#endif

            for(BLASLONG k=1; k<K; k++) {
                B0 = B[bi+0];
                B1 = B[bi+1];
                B2 = B[bi+2];
                B3 = B[bi+3];
                B4 = B[bi+4];
                B5 = B[bi+5];
                B6 = B[bi+6];
                B7 = B[bi+7];
                bi += 8;

                A0 = __riscv_vle16_v_f16mf2( &A[ai+0*gvl], gvl );
                ai += 8;

#ifdef FP16_NARROW
                result0 = __riscv_vfmacc_vf_f16mf2(result0, B0, A0, gvl);
                result1 = __riscv_vfmacc_vf_f16mf2(result1, B1, A0, gvl);
                result2 = __riscv_vfmacc_vf_f16mf2(result2, B2, A0, gvl);
                result3 = __riscv_vfmacc_vf_f16mf2(result3, B3, A0, gvl);
                result4 = __riscv_vfmacc_vf_f16mf2(result4, B4, A0, gvl);
                result5 = __riscv_vfmacc_vf_f16mf2(result5, B5, A0, gvl);
                result6 = __riscv_vfmacc_vf_f16mf2(result6, B6, A0, gvl);
                result7 = __riscv_vfmacc_vf_f16mf2(result7, B7, A0, gvl);
#else
                result0 = __riscv_vfwmacc_vf_f32m1(result0, B0, A0, gvl);
                result1 = __riscv_vfwmacc_vf_f32m1(result1, B1, A0, gvl);
                result2 = __riscv_vfwmacc_vf_f32m1(result2, B2, A0, gvl);
                result3 = __riscv_vfwmacc_vf_f32m1(result3, B3, A0, gvl);
                result4 = __riscv_vfwmacc_vf_f32m1(result4, B4, A0, gvl);
                result5 = __riscv_vfwmacc_vf_f32m1(result5, B5, A0, gvl);
                result6 = __riscv_vfwmacc_vf_f32m1(result6, B6, A0, gvl);
                result7 = __riscv_vfwmacc_vf_f32m1(result7, B7, A0, gvl);
#endif
            }

            BLASLONG ci=n_top*ldc+m_top;

            vfloat32m1_t c0 = __riscv_vle32_v_f32m1( &C[ci], gvl); ci += ldc-gvl*0;
            vfloat32m1_t c1 = __riscv_vle32_v_f32m1( &C[ci], gvl); ci += ldc-gvl*0;
            vfloat32m1_t c2 = __riscv_vle32_v_f32m1( &C[ci], gvl); ci += ldc-gvl*0;
            vfloat32m1_t c3 = __riscv_vle32_v_f32m1( &C[ci], gvl); ci += ldc-gvl*0;
            vfloat32m1_t c4 = __riscv_vle32_v_f32m1( &C[ci], gvl); ci += ldc-gvl*0;
            vfloat32m1_t c5 = __riscv_vle32_v_f32m1( &C[ci], gvl); ci += ldc-gvl*0;
            vfloat32m1_t c6 = __riscv_vle32_v_f32m1( &C[ci], gvl); ci += ldc-gvl*0;
            vfloat32m1_t c7 = __riscv_vle32_v_f32m1( &C[ci], gvl);

#ifdef FP16_NARROW
            c0 = __riscv_vfwmacc_vf_f32m1(c0, alpha16, result0, gvl);
            c1 = __riscv_vfwmacc_vf_f32m1(c1, alpha16, result1, gvl);
            c2 = __riscv_vfwmacc_vf_f32m1(c2, alpha16, result2, gvl);
            c3 = __riscv_vfwmacc_vf_f32m1(c3, alpha16, result3, gvl);
            c4 = __riscv_vfwmacc_vf_f32m1(c4, alpha16, result4, gvl);
            c5 = __riscv_vfwmacc_vf_f32m1(c5, alpha16, result5, gvl);
            c6 = __riscv_vfwmacc_vf_f32m1(c6, alpha16, result6, gvl);
            c7 = __riscv_vfwmacc_vf_f32m1(c7, alpha16, result7, gvl);
#else
            c0 = __riscv_vfmacc_vf_f32m1(c0, alpha, result0, gvl);
            c1 = __riscv_vfmacc_vf_f32m1(c1, alpha, result1, gvl);
            c2 = __riscv_vfmacc_vf_f32m1(c2, alpha, result2, gvl);
            c3 = __riscv_vfmacc_vf_f32m1(c3, alpha, result3, gvl);
            c4 = __riscv_vfmacc_vf_f32m1(c4, alpha, result4, gvl);
            c5 = __riscv_vfmacc_vf_f32m1(c5, alpha, result5, gvl);
            c6 = __riscv_vfmacc_vf_f32m1(c6, alpha, result6, gvl);
            c7 = __riscv_vfmacc_vf_f32m1(c7, alpha, result7, gvl);
#endif

            ci=n_top*ldc+m_top;

            __riscv_vse32_v_f32m1(&C[ci], c0, gvl); ci += ldc - gvl * 0;
            __riscv_vse32_v_f32m1(&C[ci], c1, gvl); ci += ldc - gvl * 0;
            __riscv_vse32_v_f32m1(&C[ci], c2, gvl); ci += ldc - gvl * 0;
            __riscv_vse32_v_f32m1(&C[ci], c3, gvl); ci += ldc - gvl * 0;
            __riscv_vse32_v_f32m1(&C[ci], c4, gvl); ci += ldc - gvl * 0;
            __riscv_vse32_v_f32m1(&C[ci], c5, gvl); ci += ldc - gvl * 0;
            __riscv_vse32_v_f32m1(&C[ci], c6, gvl); ci += ldc - gvl * 0;
            __riscv_vse32_v_f32m1(&C[ci], c7, gvl); 
            m_top += 8;
        }


        if( M & 4 ) {
            gvl = __riscv_vsetvl_e16mf2(4);

            BLASLONG ai=m_top*K;
            BLASLONG bi=n_top*K;
            _Float16 B0 = B[bi+0];
            _Float16 B1 = B[bi+1];
            _Float16 B2 = B[bi+2];
            _Float16 B3 = B[bi+3];
            _Float16 B4 = B[bi+4];
            _Float16 B5 = B[bi+5];
            _Float16 B6 = B[bi+6];
            _Float16 B7 = B[bi+7];
            bi += 8;

            vfloat16mf2_t A0 = __riscv_vle16_v_f16mf2( &A[ai+0*gvl], gvl );
            ai += 4;

#ifdef FP16_NARROW
            vfloat16mf2_t result0 = __riscv_vfmul_vf_f16mf2( A0, B0, gvl);
            vfloat16mf2_t result1 = __riscv_vfmul_vf_f16mf2( A0, B1, gvl);
            vfloat16mf2_t result2 = __riscv_vfmul_vf_f16mf2( A0, B2, gvl);
            vfloat16mf2_t result3 = __riscv_vfmul_vf_f16mf2( A0, B3, gvl);
            vfloat16mf2_t result4 = __riscv_vfmul_vf_f16mf2( A0, B4, gvl);
            vfloat16mf2_t result5 = __riscv_vfmul_vf_f16mf2( A0, B5, gvl);
            vfloat16mf2_t result6 = __riscv_vfmul_vf_f16mf2( A0, B6, gvl);
            vfloat16mf2_t result7 = __riscv_vfmul_vf_f16mf2( A0, B7, gvl);
#else
            vfloat32m1_t result0 = __riscv_vfwmul_vf_f32m1( A0, B0, gvl);
            vfloat32m1_t result1 = __riscv_vfwmul_vf_f32m1( A0, B1, gvl);
            vfloat32m1_t result2 = __riscv_vfwmul_vf_f32m1( A0, B2, gvl);
            vfloat32m1_t result3 = __riscv_vfwmul_vf_f32m1( A0, B3, gvl);
            vfloat32m1_t result4 = __riscv_vfwmul_vf_f32m1( A0, B4, gvl);
            vfloat32m1_t result5 = __riscv_vfwmul_vf_f32m1( A0, B5, gvl);
            vfloat32m1_t result6 = __riscv_vfwmul_vf_f32m1( A0, B6, gvl);
            vfloat32m1_t result7 = __riscv_vfwmul_vf_f32m1( A0, B7, gvl);
#endif

            for(BLASLONG k=1; k < K; ++k) {
                B0 = B[bi+0];
                B1 = B[bi+1];
                B2 = B[bi+2];
                B3 = B[bi+3];
                B4 = B[bi+4];
                B5 = B[bi+5];
                B6 = B[bi+6];
                B7 = B[bi+7];
                bi += 8;

                A0 = __riscv_vle16_v_f16mf2( &A[ai+0*gvl], gvl );
                ai += 4;

#ifdef FP16_NARROW
                result0 = __riscv_vfmacc_vf_f16mf2(result0, B0, A0, gvl);
                result1 = __riscv_vfmacc_vf_f16mf2(result1, B1, A0, gvl);
                result2 = __riscv_vfmacc_vf_f16mf2(result2, B2, A0, gvl);
                result3 = __riscv_vfmacc_vf_f16mf2(result3, B3, A0, gvl);
                result4 = __riscv_vfmacc_vf_f16mf2(result4, B4, A0, gvl);
                result5 = __riscv_vfmacc_vf_f16mf2(result5, B5, A0, gvl);
                result6 = __riscv_vfmacc_vf_f16mf2(result6, B6, A0, gvl);
                result7 = __riscv_vfmacc_vf_f16mf2(result7, B7, A0, gvl);
#else
                result0 = __riscv_vfwmacc_vf_f32m1(result0, B0, A0, gvl);
                result1 = __riscv_vfwmacc_vf_f32m1(result1, B1, A0, gvl);
                result2 = __riscv_vfwmacc_vf_f32m1(result2, B2, A0, gvl);
                result3 = __riscv_vfwmacc_vf_f32m1(result3, B3, A0, gvl);
                result4 = __riscv_vfwmacc_vf_f32m1(result4, B4, A0, gvl);
                result5 = __riscv_vfwmacc_vf_f32m1(result5, B5, A0, gvl);
                result6 = __riscv_vfwmacc_vf_f32m1(result6, B6, A0, gvl);
                result7 = __riscv_vfwmacc_vf_f32m1(result7, B7, A0, gvl);
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

#ifdef FP16_NARROW
            c0 = __riscv_vfwmacc_vf_f32m1(c0, alpha16, result0, gvl);
            c1 = __riscv_vfwmacc_vf_f32m1(c1, alpha16, result1, gvl);
            c2 = __riscv_vfwmacc_vf_f32m1(c2, alpha16, result2, gvl);
            c3 = __riscv_vfwmacc_vf_f32m1(c3, alpha16, result3, gvl);
            c4 = __riscv_vfwmacc_vf_f32m1(c4, alpha16, result4, gvl);
            c5 = __riscv_vfwmacc_vf_f32m1(c5, alpha16, result5, gvl);
            c6 = __riscv_vfwmacc_vf_f32m1(c6, alpha16, result6, gvl);
            c7 = __riscv_vfwmacc_vf_f32m1(c7, alpha16, result7, gvl);
#else
            c0 = __riscv_vfmacc_vf_f32m1(c0, alpha, result0, gvl);
            c1 = __riscv_vfmacc_vf_f32m1(c1, alpha, result1, gvl);
            c2 = __riscv_vfmacc_vf_f32m1(c2, alpha, result2, gvl);
            c3 = __riscv_vfmacc_vf_f32m1(c3, alpha, result3, gvl);
            c4 = __riscv_vfmacc_vf_f32m1(c4, alpha, result4, gvl);
            c5 = __riscv_vfmacc_vf_f32m1(c5, alpha, result5, gvl);
            c6 = __riscv_vfmacc_vf_f32m1(c6, alpha, result6, gvl);
            c7 = __riscv_vfmacc_vf_f32m1(c7, alpha, result7, gvl);
#endif

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

        if( M & 2 ) {
#ifdef FP16_NARROW
            _Float16 result0 = 0;
            _Float16 result1 = 0;
            _Float16 result2 = 0;
            _Float16 result3 = 0;
            _Float16 result4 = 0;
            _Float16 result5 = 0;
            _Float16 result6 = 0;
            _Float16 result7 = 0;
            _Float16 result8 = 0;
            _Float16 result9 = 0;
            _Float16 result10 = 0;
            _Float16 result11 = 0;
            _Float16 result12 = 0;
            _Float16 result13 = 0;
            _Float16 result14 = 0;
            _Float16 result15 = 0;
#else
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
#endif
            BLASLONG ai = m_top * K;
            BLASLONG bi = n_top * K;
            
            for(BLASLONG k=0; k<K; k++) {
#ifdef FP16_NARROW
                result0+=A[ai+0]*B[bi+0];
                result1+=A[ai+1]*B[bi+0];
                result2+=A[ai+0]*B[bi+1];
                result3+=A[ai+1]*B[bi+1];
                result4+=A[ai+0]*B[bi+2];
                result5+=A[ai+1]*B[bi+2];
                result6+=A[ai+0]*B[bi+3];
                result7+=A[ai+1]*B[bi+3];
                result8+=A[ai+0]*B[bi+4];
                result9+=A[ai+1]*B[bi+4];
                result10+=A[ai+0]*B[bi+5];
                result11+=A[ai+1]*B[bi+5];
                result12+=A[ai+0]*B[bi+6];
                result13+=A[ai+1]*B[bi+6];
                result14+=A[ai+0]*B[bi+7];
                result15+=A[ai+1]*B[bi+7];
#else
                result0+=(float)(A[ai+0])*(float)(B[bi+0]);
                result1+=(float)(A[ai+1])*(float)(B[bi+0]);
                result2+=(float)(A[ai+0])*(float)(B[bi+1]);
                result3+=(float)(A[ai+1])*(float)(B[bi+1]);
                result4+=(float)(A[ai+0])*(float)(B[bi+2]);
                result5+=(float)(A[ai+1])*(float)(B[bi+2]);
                result6+=(float)(A[ai+0])*(float)(B[bi+3]);
                result7+=(float)(A[ai+1])*(float)(B[bi+3]);
                result8+=(float)(A[ai+0])*(float)(B[bi+4]);
                result9+=(float)(A[ai+1])*(float)(B[bi+4]);
                result10+=(float)(A[ai+0])*(float)(B[bi+5]);
                result11+=(float)(A[ai+1])*(float)(B[bi+5]);
                result12+=(float)(A[ai+0])*(float)(B[bi+6]);
                result13+=(float)(A[ai+1])*(float)(B[bi+6]);
                result14+=(float)(A[ai+0])*(float)(B[bi+7]);
                result15+=(float)(A[ai+1])*(float)(B[bi+7]);
#endif
                ai+=2;
                bi+=8;
            }

            BLASLONG ci=n_top*ldc+m_top;

#ifdef FP16_NARROW
            C[ci + 0 * ldc + 0] += alpha16 * (float)(result0);
            C[ci + 0 * ldc + 1] += alpha16 * (float)(result1);
            C[ci + 1 * ldc + 0] += alpha16 * (float)(result2);
            C[ci + 1 * ldc + 1] += alpha16 * (float)(result3);
            C[ci + 2 * ldc + 0] += alpha16 * (float)(result4);
            C[ci + 2 * ldc + 1] += alpha16 * (float)(result5);
            C[ci + 3 * ldc + 0] += alpha16 * (float)(result6);
            C[ci + 3 * ldc + 1] += alpha16 * (float)(result7);
            C[ci + 4 * ldc + 0] += alpha16 * (float)(result8);
            C[ci + 4 * ldc + 1] += alpha16 * (float)(result9);
            C[ci + 5 * ldc + 0] += alpha16 * (float)(result10);
            C[ci + 5 * ldc + 1] += alpha16 * (float)(result11);
            C[ci + 6 * ldc + 0] += alpha16 * (float)(result12);
            C[ci + 6 * ldc + 1] += alpha16 * (float)(result13);
            C[ci + 7 * ldc + 0] += alpha16 * (float)(result14);
            C[ci + 7 * ldc + 1] += alpha16 * (float)(result15);
#else
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
#endif

            m_top+=2;
        }


        if( M & 1 ) {
            
#ifdef FP16_NARROW
            _Float16 result0 = 0;
            _Float16 result1 = 0;
            _Float16 result2 = 0;
            _Float16 result3 = 0;
            _Float16 result4 = 0;
            _Float16 result5 = 0;
            _Float16 result6 = 0;
            _Float16 result7 = 0;
#else
            float result0 = 0;
            float result1 = 0;
            float result2 = 0;
            float result3 = 0;
            float result4 = 0;
            float result5 = 0;
            float result6 = 0;
            float result7 = 0;
#endif
            
            BLASLONG ai = m_top * K;
            BLASLONG bi = n_top * K;

            for(BLASLONG k=0; k<K; k++) {
#ifdef FP16_NARROW
                result0+=A[ai+0]*B[bi+0];
                result1+=A[ai+0]*B[bi+1];
                result2+=A[ai+0]*B[bi+2];
                result3+=A[ai+0]*B[bi+3];
                result4+=A[ai+0]*B[bi+4];
                result5+=A[ai+0]*B[bi+5];
                result6+=A[ai+0]*B[bi+6];
                result7+=A[ai+0]*B[bi+7];
#else
                result0+=(float)(A[ai+0])*(float)(B[bi+0]);
                result1+=(float)(A[ai+0])*(float)(B[bi+1]);
                result2+=(float)(A[ai+0])*(float)(B[bi+2]);
                result3+=(float)(A[ai+0])*(float)(B[bi+3]);
                result4+=(float)(A[ai+0])*(float)(B[bi+4]);
                result5+=(float)(A[ai+0])*(float)(B[bi+5]);
                result6+=(float)(A[ai+0])*(float)(B[bi+6]);
                result7+=(float)(A[ai+0])*(float)(B[bi+7]);
#endif
                ai+=1;
                bi+=8;
            }

            BLASLONG ci = n_top * ldc + m_top;
#ifdef FP16_NARROW
            C[ci + 0 * ldc + 0] += alpha16 * (float)(result0);
            C[ci + 1 * ldc + 0] += alpha16 * (float)(result1);
            C[ci + 2 * ldc + 0] += alpha16 * (float)(result2);
            C[ci + 3 * ldc + 0] += alpha16 * (float)(result3);
            C[ci + 4 * ldc + 0] += alpha16 * (float)(result4);
            C[ci + 5 * ldc + 0] += alpha16 * (float)(result5);
            C[ci + 6 * ldc + 0] += alpha16 * (float)(result6);
            C[ci + 7 * ldc + 0] += alpha16 * (float)(result7);
#else
            C[ci + 0 * ldc + 0] += alpha * result0;
            C[ci + 1 * ldc + 0] += alpha * result1;
            C[ci + 2 * ldc + 0] += alpha * result2;
            C[ci + 3 * ldc + 0] += alpha * result3;
            C[ci + 4 * ldc + 0] += alpha * result4;
            C[ci + 5 * ldc + 0] += alpha * result5;
            C[ci + 6 * ldc + 0] += alpha * result6;
            C[ci + 7 * ldc + 0] += alpha * result7;
#endif
            m_top+=1;
        }
        n_top += 8;
    }

    if( N & 4 ) {
        m_top = 0;
#ifdef FP16_NARROW
        gvl = __riscv_vsetvl_e16m2(32);

        for (BLASLONG i=0; i<M/32; i+=1) {
            BLASLONG ai=m_top*K;
            BLASLONG bi=n_top*K;

            _Float16 B0 = B[bi+0];
            _Float16 B1 = B[bi+1];
            _Float16 B2 = B[bi+2];
            _Float16 B3 = B[bi+3];
            bi += 4;

            vfloat16m1_t A00 = __riscv_vle16_v_f16m1( &A[ai+0*gvl], 16 );
            vfloat16m1_t A01 = __riscv_vle16_v_f16m1( &A[ai+0*gvl+16*K], 16 );
            vfloat16m2_t A0 = __riscv_vcreate_v_f16m1_f16m2(A00, A01);
            ai += 16;

            vfloat16m2_t result0 = __riscv_vfmul_vf_f16m2( A0, B0, gvl);
            vfloat16m2_t result1 = __riscv_vfmul_vf_f16m2( A0, B1, gvl);
            vfloat16m2_t result2 = __riscv_vfmul_vf_f16m2( A0, B2, gvl);
            vfloat16m2_t result3 = __riscv_vfmul_vf_f16m2( A0, B3, gvl);

            for(BLASLONG k=1; k<K; k++) {
                B0 = B[bi+0];
                B1 = B[bi+1];
                B2 = B[bi+2];
                B3 = B[bi+3];
                bi += 4;

                A00 = __riscv_vle16_v_f16m1( &A[ai+0*gvl], 16 );
                A01 = __riscv_vle16_v_f16m1( &A[ai+0*gvl+16*K], 16 );
                A0 = __riscv_vcreate_v_f16m1_f16m2(A00, A01);
                ai += 16;

                result0 = __riscv_vfmacc_vf_f16m2(result0, B0, A0, gvl);
                result1 = __riscv_vfmacc_vf_f16m2(result1, B1, A0, gvl);
                result2 = __riscv_vfmacc_vf_f16m2(result2, B2, A0, gvl);
                result3 = __riscv_vfmacc_vf_f16m2(result3, B3, A0, gvl);
            }

            BLASLONG ci=n_top*ldc+m_top;

            vfloat32m4_t c0 = __riscv_vle32_v_f32m4( &C[ci], gvl); ci += ldc-gvl*0;
            vfloat32m4_t c1 = __riscv_vle32_v_f32m4( &C[ci], gvl); ci += ldc-gvl*0;
            vfloat32m4_t c2 = __riscv_vle32_v_f32m4( &C[ci], gvl); ci += ldc-gvl*0;
            vfloat32m4_t c3 = __riscv_vle32_v_f32m4( &C[ci], gvl);

            c0 = __riscv_vfwmacc_vf_f32m4(c0, alpha16, result0, gvl);
            c1 = __riscv_vfwmacc_vf_f32m4(c1, alpha16, result1, gvl);
            c2 = __riscv_vfwmacc_vf_f32m4(c2, alpha16, result2, gvl);
            c3 = __riscv_vfwmacc_vf_f32m4(c3, alpha16, result3, gvl);

            ci=n_top*ldc+m_top;

            __riscv_vse32_v_f32m4( &C[ci], c0, gvl); ci += ldc-gvl*0;
            __riscv_vse32_v_f32m4( &C[ci], c1, gvl); ci += ldc-gvl*0;
            __riscv_vse32_v_f32m4( &C[ci], c2, gvl); ci += ldc-gvl*0;
            __riscv_vse32_v_f32m4( &C[ci], c3, gvl);
            m_top += 32;
        }

        if (M & 16) {
          gvl = __riscv_vsetvl_e16m1(16);
#else
        gvl = __riscv_vsetvl_e16m1(16);

        for (BLASLONG i=0; i<M/16; i+=1) {
#endif
            BLASLONG ai=m_top*K;	
            BLASLONG bi=n_top*K;	
            
            _Float16 B0 = B[bi+0];
            _Float16 B1 = B[bi+1];
            _Float16 B2 = B[bi+2];
            _Float16 B3 = B[bi+3];
            bi += 4;

            
            vfloat16m1_t A0 = __riscv_vle16_v_f16m1( &A[ai+0*gvl], gvl );
            ai += 16;

#ifdef FP16_NARROW
            vfloat16m1_t result0 = __riscv_vfmul_vf_f16m1( A0, B0, gvl);
            vfloat16m1_t result1 = __riscv_vfmul_vf_f16m1( A0, B1, gvl);
            vfloat16m1_t result2 = __riscv_vfmul_vf_f16m1( A0, B2, gvl);
            vfloat16m1_t result3 = __riscv_vfmul_vf_f16m1( A0, B3, gvl);
#else
            vfloat32m2_t result0 = __riscv_vfwmul_vf_f32m2( A0, B0, gvl);
            vfloat32m2_t result1 = __riscv_vfwmul_vf_f32m2( A0, B1, gvl);
            vfloat32m2_t result2 = __riscv_vfwmul_vf_f32m2( A0, B2, gvl);
            vfloat32m2_t result3 = __riscv_vfwmul_vf_f32m2( A0, B3, gvl);
#endif
            
            for(BLASLONG k=1; k<K; k++) {
                B0 = B[bi+0];
                B1 = B[bi+1];
                B2 = B[bi+2];
                B3 = B[bi+3];
                bi += 4;

                A0 = __riscv_vle16_v_f16m1( &A[ai+0*gvl], gvl );
                ai += 16;
                
#ifdef FP16_NARROW
                result0 = __riscv_vfmacc_vf_f16m1(result0, B0, A0, gvl);
                result1 = __riscv_vfmacc_vf_f16m1(result1, B1, A0, gvl);
                result2 = __riscv_vfmacc_vf_f16m1(result2, B2, A0, gvl);
                result3 = __riscv_vfmacc_vf_f16m1(result3, B3, A0, gvl);
#else
                result0 = __riscv_vfwmacc_vf_f32m2(result0, B0, A0, gvl);
                result1 = __riscv_vfwmacc_vf_f32m2(result1, B1, A0, gvl);
                result2 = __riscv_vfwmacc_vf_f32m2(result2, B2, A0, gvl);
                result3 = __riscv_vfwmacc_vf_f32m2(result3, B3, A0, gvl);
#endif
            }
            
            BLASLONG ci=n_top*ldc+m_top;

            vfloat32m2_t c0 = __riscv_vle32_v_f32m2( &C[ci], gvl); ci += ldc-gvl*0;
            vfloat32m2_t c1 = __riscv_vle32_v_f32m2( &C[ci], gvl); ci += ldc-gvl*0;
            vfloat32m2_t c2 = __riscv_vle32_v_f32m2( &C[ci], gvl); ci += ldc-gvl*0;
            vfloat32m2_t c3 = __riscv_vle32_v_f32m2( &C[ci], gvl);

#ifdef FP16_NARROW
            c0 = __riscv_vfwmacc_vf_f32m2(c0, alpha16, result0, gvl);
            c1 = __riscv_vfwmacc_vf_f32m2(c1, alpha16, result1, gvl);
            c2 = __riscv_vfwmacc_vf_f32m2(c2, alpha16, result2, gvl);
            c3 = __riscv_vfwmacc_vf_f32m2(c3, alpha16, result3, gvl);
#else
            c0 = __riscv_vfmacc_vf_f32m2(c0, alpha, result0, gvl);
            c1 = __riscv_vfmacc_vf_f32m2(c1, alpha, result1, gvl);
            c2 = __riscv_vfmacc_vf_f32m2(c2, alpha, result2, gvl);
            c3 = __riscv_vfmacc_vf_f32m2(c3, alpha, result3, gvl);
#endif

            ci=n_top*ldc+m_top;

            __riscv_vse32_v_f32m2( &C[ci], c0, gvl); ci += ldc-gvl*0;
            __riscv_vse32_v_f32m2( &C[ci], c1, gvl); ci += ldc-gvl*0;
            __riscv_vse32_v_f32m2( &C[ci], c2, gvl); ci += ldc-gvl*0;
            __riscv_vse32_v_f32m2( &C[ci], c3, gvl);
            m_top += 16;
        }

        if( M & 8 ) {
            gvl = __riscv_vsetvl_e16mf2(8);
            BLASLONG ai=m_top*K;	
            BLASLONG bi=n_top*K;	
            
            _Float16 B0 = B[bi+0];
            _Float16 B1 = B[bi+1];
            _Float16 B2 = B[bi+2];
            _Float16 B3 = B[bi+3];
            bi += 4;

            vfloat16mf2_t A0 = __riscv_vle16_v_f16mf2( &A[ai+0*gvl], gvl );
            ai += 8;

#ifdef FP16_NARROW
            vfloat16mf2_t result0 = __riscv_vfmul_vf_f16mf2( A0, B0, gvl);
            vfloat16mf2_t result1 = __riscv_vfmul_vf_f16mf2( A0, B1, gvl);
            vfloat16mf2_t result2 = __riscv_vfmul_vf_f16mf2( A0, B2, gvl);
            vfloat16mf2_t result3 = __riscv_vfmul_vf_f16mf2( A0, B3, gvl);
#else
            vfloat32m1_t result0 = __riscv_vfwmul_vf_f32m1( A0, B0, gvl);
            vfloat32m1_t result1 = __riscv_vfwmul_vf_f32m1( A0, B1, gvl);
            vfloat32m1_t result2 = __riscv_vfwmul_vf_f32m1( A0, B2, gvl);
            vfloat32m1_t result3 = __riscv_vfwmul_vf_f32m1( A0, B3, gvl);
#endif
		
            for(BLASLONG k=1; k<K; k++) {
                B0 = B[bi+0];
                B1 = B[bi+1];
                B2 = B[bi+2];
                B3 = B[bi+3];
                bi += 4;

                A0 = __riscv_vle16_v_f16mf2( &A[ai+0*gvl], gvl );
                ai += 8;

#ifdef FP16_NARROW
                result0 = __riscv_vfmacc_vf_f16mf2(result0, B0, A0, gvl);
                result1 = __riscv_vfmacc_vf_f16mf2(result1, B1, A0, gvl);
                result2 = __riscv_vfmacc_vf_f16mf2(result2, B2, A0, gvl);
                result3 = __riscv_vfmacc_vf_f16mf2(result3, B3, A0, gvl);
#else
                result0 = __riscv_vfwmacc_vf_f32m1(result0, B0, A0, gvl);
                result1 = __riscv_vfwmacc_vf_f32m1(result1, B1, A0, gvl);
                result2 = __riscv_vfwmacc_vf_f32m1(result2, B2, A0, gvl);
                result3 = __riscv_vfwmacc_vf_f32m1(result3, B3, A0, gvl);
#endif
            }
            

            BLASLONG ci=n_top*ldc+m_top;

            vfloat32m1_t c0 = __riscv_vle32_v_f32m1( &C[ci], gvl); ci += ldc - gvl * 0;
            vfloat32m1_t c1 = __riscv_vle32_v_f32m1( &C[ci], gvl); ci += ldc - gvl * 0;
            vfloat32m1_t c2 = __riscv_vle32_v_f32m1( &C[ci], gvl); ci += ldc - gvl * 0;
            vfloat32m1_t c3 = __riscv_vle32_v_f32m1( &C[ci], gvl);
            
#ifdef FP16_NARROW
            c0 = __riscv_vfwmacc_vf_f32m1(c0, alpha16, result0, gvl);
            c1 = __riscv_vfwmacc_vf_f32m1(c1, alpha16, result1, gvl);
            c2 = __riscv_vfwmacc_vf_f32m1(c2, alpha16, result2, gvl);
            c3 = __riscv_vfwmacc_vf_f32m1(c3, alpha16, result3, gvl);
#else
            c0 = __riscv_vfmacc_vf_f32m1(c0, alpha, result0, gvl);
            c1 = __riscv_vfmacc_vf_f32m1(c1, alpha, result1, gvl);
            c2 = __riscv_vfmacc_vf_f32m1(c2, alpha, result2, gvl);
            c3 = __riscv_vfmacc_vf_f32m1(c3, alpha, result3, gvl);
#endif

            ci = n_top * ldc + m_top;

            __riscv_vse32_v_f32m1( &C[ci], c0, gvl); ci += ldc-gvl*0;
            __riscv_vse32_v_f32m1( &C[ci], c1, gvl); ci += ldc-gvl*0;
            __riscv_vse32_v_f32m1( &C[ci], c2, gvl); ci += ldc-gvl*0;
            __riscv_vse32_v_f32m1( &C[ci], c3, gvl);
            m_top += 8;
        }

        if( M & 4 ) {
            gvl = __riscv_vsetvl_e16mf2(4);

            BLASLONG ai=m_top*K;
            BLASLONG bi=n_top*K;
            _Float16 B0 = B[bi+0];
            _Float16 B1 = B[bi+1];
            _Float16 B2 = B[bi+2];
            _Float16 B3 = B[bi+3];
            bi += 4;

            vfloat16mf2_t A0 = __riscv_vle16_v_f16mf2( &A[ai+0*gvl], gvl );
            ai += 4;

#ifdef FP16_NARROW
            vfloat16mf2_t result0 = __riscv_vfmul_vf_f16mf2( A0, B0, gvl);
            vfloat16mf2_t result1 = __riscv_vfmul_vf_f16mf2( A0, B1, gvl);
            vfloat16mf2_t result2 = __riscv_vfmul_vf_f16mf2( A0, B2, gvl);
            vfloat16mf2_t result3 = __riscv_vfmul_vf_f16mf2( A0, B3, gvl);
#else
            vfloat32m1_t result0 = __riscv_vfwmul_vf_f32m1( A0, B0, gvl);
            vfloat32m1_t result1 = __riscv_vfwmul_vf_f32m1( A0, B1, gvl);
            vfloat32m1_t result2 = __riscv_vfwmul_vf_f32m1( A0, B2, gvl);
            vfloat32m1_t result3 = __riscv_vfwmul_vf_f32m1( A0, B3, gvl);
#endif

            for(BLASLONG k=1; k < K; ++k) {
                B0 = B[bi+0];
                B1 = B[bi+1];
                B2 = B[bi+2];
                B3 = B[bi+3];
                bi += 4;

                A0 = __riscv_vle16_v_f16mf2( &A[ai+0*gvl], gvl );
                ai += 4;

#ifdef FP16_NARROW
                result0 = __riscv_vfmacc_vf_f16mf2(result0, B0, A0, gvl);
                result1 = __riscv_vfmacc_vf_f16mf2(result1, B1, A0, gvl);
                result2 = __riscv_vfmacc_vf_f16mf2(result2, B2, A0, gvl);
                result3 = __riscv_vfmacc_vf_f16mf2(result3, B3, A0, gvl);
#else
                result0 = __riscv_vfwmacc_vf_f32m1(result0, B0, A0, gvl);
                result1 = __riscv_vfwmacc_vf_f32m1(result1, B1, A0, gvl);
                result2 = __riscv_vfwmacc_vf_f32m1(result2, B2, A0, gvl);
                result3 = __riscv_vfwmacc_vf_f32m1(result3, B3, A0, gvl);
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
#ifdef FP16_NARROW
            c0 = __riscv_vfwmacc_vf_f32m1(c0, alpha16, result0, gvl);
            c1 = __riscv_vfwmacc_vf_f32m1(c1, alpha16, result1, gvl);
            c2 = __riscv_vfwmacc_vf_f32m1(c2, alpha16, result2, gvl);
            c3 = __riscv_vfwmacc_vf_f32m1(c3, alpha16, result3, gvl);
#else
            c0 = __riscv_vfmacc_vf_f32m1(c0, alpha, result0, gvl);
            c1 = __riscv_vfmacc_vf_f32m1(c1, alpha, result1, gvl);
            c2 = __riscv_vfmacc_vf_f32m1(c2, alpha, result2, gvl);
            c3 = __riscv_vfmacc_vf_f32m1(c3, alpha, result3, gvl);
#endif

            ci= n_top * ldc + m_top;

            __riscv_vse32_v_f32m1(&C[ci], c0, gvl); ci += ldc - gvl * 0;
            __riscv_vse32_v_f32m1(&C[ci], c1, gvl); ci += ldc - gvl * 0;
            __riscv_vse32_v_f32m1(&C[ci], c2, gvl); ci += ldc - gvl * 0;
            __riscv_vse32_v_f32m1(&C[ci], c3, gvl); 
            
            m_top += 4;
        }


        if( M & 2 ) {
#ifdef FP16_NARROW
            _Float16 result0 = 0;
            _Float16 result1 = 0;
            _Float16 result2 = 0;
            _Float16 result3 = 0;
            _Float16 result4 = 0;
            _Float16 result5 = 0;
            _Float16 result6 = 0;
            _Float16 result7 = 0;
#else
            float result0 = 0;
            float result1 = 0;
            float result2 = 0;
            float result3 = 0;
            float result4 = 0;
            float result5 = 0;
            float result6 = 0;
            float result7 = 0;
#endif
            BLASLONG ai = m_top * K;
            BLASLONG bi = n_top * K;

            for(BLASLONG k=0; k<K; k++) {
#ifdef FP16_NARROW
                result0+=A[ai+0]*B[bi+0];
                result1+=A[ai+1]*B[bi+0];
                result2+=A[ai+0]*B[bi+1];
                result3+=A[ai+1]*B[bi+1];
                result4+=A[ai+0]*B[bi+2];
                result5+=A[ai+1]*B[bi+2];
                result6+=A[ai+0]*B[bi+3];
                result7+=A[ai+1]*B[bi+3];
#else
                result0+=(float)(A[ai+0])*(float)(B[bi+0]);
                result1+=(float)(A[ai+1])*(float)(B[bi+0]);
                result2+=(float)(A[ai+0])*(float)(B[bi+1]);
                result3+=(float)(A[ai+1])*(float)(B[bi+1]);
                result4+=(float)(A[ai+0])*(float)(B[bi+2]);
                result5+=(float)(A[ai+1])*(float)(B[bi+2]);
                result6+=(float)(A[ai+0])*(float)(B[bi+3]);
                result7+=(float)(A[ai+1])*(float)(B[bi+3]);
#endif
                ai+=2;
                bi+=4;
            }
            
            
            BLASLONG ci=n_top*ldc+m_top;
#ifdef FP16_NARROW
            C[ci + 0 * ldc + 0] += alpha16 * (float)(result0);
            C[ci + 0 * ldc + 1] += alpha16 * (float)(result1);
            C[ci + 1 * ldc + 0] += alpha16 * (float)(result2);
            C[ci + 1 * ldc + 1] += alpha16 * (float)(result3);
            C[ci + 2 * ldc + 0] += alpha16 * (float)(result4);
            C[ci + 2 * ldc + 1] += alpha16 * (float)(result5);
            C[ci + 3 * ldc + 0] += alpha16 * (float)(result6);
            C[ci + 3 * ldc + 1] += alpha16 * (float)(result7);
#else
            C[ci + 0 * ldc + 0] += alpha * result0;
            C[ci + 0 * ldc + 1] += alpha * result1;
            C[ci + 1 * ldc + 0] += alpha * result2;
            C[ci + 1 * ldc + 1] += alpha * result3;
            C[ci + 2 * ldc + 0] += alpha * result4;
            C[ci + 2 * ldc + 1] += alpha * result5;
            C[ci + 3 * ldc + 0] += alpha * result6;
            C[ci + 3 * ldc + 1] += alpha * result7;
#endif

            m_top += 2;
        }


        if( M & 1 ) {
            
#ifdef FP16_NARROW
            _Float16 result0 = 0;
            _Float16 result1 = 0;
            _Float16 result2 = 0;
            _Float16 result3 = 0;
#else
            float result0 = 0;
            float result1 = 0;
            float result2 = 0;
            float result3 = 0;
#endif
            
            BLASLONG ai = m_top * K;
            BLASLONG bi = n_top * K;

            for(BLASLONG k=0; k<K; k++) {
#ifdef FP16_NARROW
                result0+=A[ai+0]*B[bi+0];
                result1+=A[ai+0]*B[bi+1];
                result2+=A[ai+0]*B[bi+2];
                result3+=A[ai+0]*B[bi+3];
#else
                result0+=(float)(A[ai+0])*(float)(B[bi+0]);
                result1+=(float)(A[ai+0])*(float)(B[bi+1]);
                result2+=(float)(A[ai+0])*(float)(B[bi+2]);
                result3+=(float)(A[ai+0])*(float)(B[bi+3]);
#endif
                ai+=1;
                bi+=4;
            }

            BLASLONG ci = n_top * ldc + m_top;
#ifdef FP16_NARROW
            C[ci + 0 * ldc + 0] += alpha16 * (float)(result0);
            C[ci + 1 * ldc + 0] += alpha16 * (float)(result1);
            C[ci + 2 * ldc + 0] += alpha16 * (float)(result2);
            C[ci + 3 * ldc + 0] += alpha16 * (float)(result3);
#else
            C[ci + 0 * ldc + 0] += alpha * result0;
            C[ci + 1 * ldc + 0] += alpha * result1;
            C[ci + 2 * ldc + 0] += alpha * result2;
            C[ci + 3 * ldc + 0] += alpha * result3;
#endif
            m_top += 1;
        }

        n_top += 4;
    }



    // -- tails for N=2
    if( N & 2 ) {
        m_top = 0;
#ifdef FP16_NARROW
        gvl = __riscv_vsetvl_e16m2(32);

        for (BLASLONG i=0; i<M/32; i+=1) {
            BLASLONG ai=m_top*K;
            BLASLONG bi=n_top*K;

            _Float16 B0 = B[bi+0];
            _Float16 B1 = B[bi+1];
            bi += 2;

            vfloat16m1_t A00 = __riscv_vle16_v_f16m1( &A[ai+0*gvl], 16 );
            vfloat16m1_t A01 = __riscv_vle16_v_f16m1( &A[ai+0*gvl+16*K], 16 );
            vfloat16m2_t A0 = __riscv_vcreate_v_f16m1_f16m2(A00, A01);
            ai += 16;

            vfloat16m2_t result0 = __riscv_vfmul_vf_f16m2( A0, B0, gvl);
            vfloat16m2_t result1 = __riscv_vfmul_vf_f16m2( A0, B1, gvl);

            for(BLASLONG k=1; k<K; k++) {
                B0 = B[bi+0];
                B1 = B[bi+1];
                bi += 2;

                A00 = __riscv_vle16_v_f16m1( &A[ai+0*gvl], 16 );
                A01 = __riscv_vle16_v_f16m1( &A[ai+0*gvl+16*K], 16 );
                A0 = __riscv_vcreate_v_f16m1_f16m2(A00, A01);
                ai += 16;

                result0 = __riscv_vfmacc_vf_f16m2(result0, B0, A0, gvl);
                result1 = __riscv_vfmacc_vf_f16m2(result1, B1, A0, gvl);
            }

            BLASLONG ci=n_top*ldc+m_top;

            vfloat32m4_t c0 = __riscv_vle32_v_f32m4( &C[ci], gvl); ci += ldc-gvl*0;
            vfloat32m4_t c1 = __riscv_vle32_v_f32m4( &C[ci], gvl);
            c0 = __riscv_vfwmacc_vf_f32m4(c0, alpha16, result0, gvl);
            c1 = __riscv_vfwmacc_vf_f32m4(c1, alpha16, result1, gvl);

            ci=n_top*ldc+m_top;

            __riscv_vse32_v_f32m4( &C[ci], c0, gvl); ci += ldc-gvl*0;
            __riscv_vse32_v_f32m4( &C[ci], c1, gvl);
            m_top += 32;
        }

        if (M & 16) {
          gvl = __riscv_vsetvl_e16m1(16);
#else
        gvl = __riscv_vsetvl_e16m1(16);

        for (BLASLONG i=0; i<M/16; i+=1) {
#endif
            BLASLONG ai=m_top*K;	
            BLASLONG bi=n_top*K;	
            
            _Float16 B0 = B[bi+0];
            _Float16 B1 = B[bi+1];
            bi += 2;

            
            vfloat16m1_t A0 = __riscv_vle16_v_f16m1( &A[ai+0*gvl], gvl );
            ai += 16;
            
#ifdef FP16_NARROW
            vfloat16m1_t result0 = __riscv_vfmul_vf_f16m1( A0, B0, gvl);
            vfloat16m1_t result1 = __riscv_vfmul_vf_f16m1( A0, B1, gvl);
#else
            vfloat32m2_t result0 = __riscv_vfwmul_vf_f32m2( A0, B0, gvl);
            vfloat32m2_t result1 = __riscv_vfwmul_vf_f32m2( A0, B1, gvl);
#endif
            
            for(BLASLONG k=1; k<K; k++) {
                B0 = B[bi+0];
                B1 = B[bi+1];
                bi += 2;

                A0 = __riscv_vle16_v_f16m1( &A[ai+0*gvl], gvl );
                ai += 16;

#ifdef FP16_NARROW
                result0 = __riscv_vfmacc_vf_f16m1(result0, B0, A0, gvl);
                result1 = __riscv_vfmacc_vf_f16m1(result1, B1, A0, gvl);
#else
                result0 = __riscv_vfwmacc_vf_f32m2(result0, B0, A0, gvl);
                result1 = __riscv_vfwmacc_vf_f32m2(result1, B1, A0, gvl);
#endif
            }

            BLASLONG ci=n_top*ldc+m_top;

            vfloat32m2_t c0 = __riscv_vle32_v_f32m2( &C[ci], gvl); ci += ldc-gvl*0;
            vfloat32m2_t c1 = __riscv_vle32_v_f32m2( &C[ci], gvl);
#ifdef FP16_NARROW
            c0 = __riscv_vfwmacc_vf_f32m2(c0, alpha16, result0, gvl);
            c1 = __riscv_vfwmacc_vf_f32m2(c1, alpha16, result1, gvl);
#else
            c0 = __riscv_vfmacc_vf_f32m2(c0, alpha, result0, gvl);
            c1 = __riscv_vfmacc_vf_f32m2(c1, alpha, result1, gvl);
#endif

            ci=n_top*ldc+m_top;

            __riscv_vse32_v_f32m2( &C[ci], c0, gvl); ci += ldc-gvl*0;
            __riscv_vse32_v_f32m2( &C[ci], c1, gvl);
            m_top += 16;
        }

        if( M & 8 ) {
            gvl = __riscv_vsetvl_e16mf2(8);
            BLASLONG ai=m_top*K;	
            BLASLONG bi=n_top*K;	
            
            _Float16 B0 = B[bi+0];
            _Float16 B1 = B[bi+1];
            bi += 2;

            vfloat16mf2_t A0 = __riscv_vle16_v_f16mf2( &A[ai+0*gvl], gvl );
            ai += 8;

#ifdef FP16_NARROW
            vfloat16mf2_t result0 = __riscv_vfmul_vf_f16mf2( A0, B0, gvl);
            vfloat16mf2_t result1 = __riscv_vfmul_vf_f16mf2( A0, B1, gvl);
#else
            vfloat32m1_t result0 = __riscv_vfwmul_vf_f32m1( A0, B0, gvl);
            vfloat32m1_t result1 = __riscv_vfwmul_vf_f32m1( A0, B1, gvl);
#endif
		
            for(BLASLONG k=1; k<K; k++) {
                B0 = B[bi+0];
                B1 = B[bi+1];
                bi += 2;

                A0 = __riscv_vle16_v_f16mf2( &A[ai+0*gvl], gvl );
                ai += 8;

#ifdef FP16_NARROW
                result0 = __riscv_vfmacc_vf_f16mf2(result0, B0, A0, gvl);
                result1 = __riscv_vfmacc_vf_f16mf2(result1, B1, A0, gvl);
#else
                result0 = __riscv_vfwmacc_vf_f32m1(result0, B0, A0, gvl);
                result1 = __riscv_vfwmacc_vf_f32m1(result1, B1, A0, gvl);
#endif
            }


            BLASLONG ci=n_top*ldc+m_top;

            vfloat32m1_t c0 = __riscv_vle32_v_f32m1( &C[ci], gvl); ci += ldc - gvl * 0;
            vfloat32m1_t c1 = __riscv_vle32_v_f32m1( &C[ci], gvl); 
#ifdef FP16_NARROW
            c0 = __riscv_vfwmacc_vf_f32m1(c0, alpha16, result0, gvl);
            c1 = __riscv_vfwmacc_vf_f32m1(c1, alpha16, result1, gvl);
#else
            c0 = __riscv_vfmacc_vf_f32m1(c0, alpha, result0, gvl);
            c1 = __riscv_vfmacc_vf_f32m1(c1, alpha, result1, gvl);
#endif

            ci = n_top * ldc + m_top;

            __riscv_vse32_v_f32m1( &C[ci], c0, gvl); ci += ldc-gvl*0;
            __riscv_vse32_v_f32m1( &C[ci], c1, gvl); 
            m_top += 8;
        }

        if( M & 4 ) {
            gvl = __riscv_vsetvl_e16mf2(4);

            BLASLONG ai=m_top*K;
            BLASLONG bi=n_top*K;
            _Float16 B0 = B[bi+0];
            _Float16 B1 = B[bi+1];
            bi += 2;

            vfloat16mf2_t A0 = __riscv_vle16_v_f16mf2( &A[ai+0*gvl], gvl );
            ai += 4;

#ifdef FP16_NARROW
            vfloat16mf2_t result0 = __riscv_vfmul_vf_f16mf2( A0, B0, gvl);
            vfloat16mf2_t result1 = __riscv_vfmul_vf_f16mf2( A0, B1, gvl);
#else
            vfloat32m1_t result0 = __riscv_vfwmul_vf_f32m1( A0, B0, gvl);
            vfloat32m1_t result1 = __riscv_vfwmul_vf_f32m1( A0, B1, gvl);
#endif

            for(BLASLONG k=1; k < K; ++k) {
                B0 = B[bi+0];
                B1 = B[bi+1];
                bi += 2;

                A0 = __riscv_vle16_v_f16mf2( &A[ai+0*gvl], gvl );
                ai += 4;

#ifdef FP16_NARROW
                result0 = __riscv_vfmacc_vf_f16mf2(result0, B0, A0, gvl);
                result1 = __riscv_vfmacc_vf_f16mf2(result1, B1, A0, gvl);
#else
                result0 = __riscv_vfwmacc_vf_f32m1(result0, B0, A0, gvl);
                result1 = __riscv_vfwmacc_vf_f32m1(result1, B1, A0, gvl);
#endif
            }

            BLASLONG ci = n_top * ldc + m_top;

            vfloat32m1_t c0 = __riscv_vle32_v_f32m1(&C[ci], gvl);
            ci += ldc - gvl * 0;
            vfloat32m1_t c1 = __riscv_vle32_v_f32m1(&C[ci], gvl);
#ifdef FP16_NARROW
            c0 = __riscv_vfwmacc_vf_f32m1(c0, alpha16, result0, gvl);
            c1 = __riscv_vfwmacc_vf_f32m1(c1, alpha16, result1, gvl);
#else
            c0 = __riscv_vfmacc_vf_f32m1(c0, alpha, result0, gvl);
            c1 = __riscv_vfmacc_vf_f32m1(c1, alpha, result1, gvl);
#endif

            ci= n_top * ldc + m_top;

            __riscv_vse32_v_f32m1(&C[ci], c0, gvl); ci += ldc - gvl * 0;
            __riscv_vse32_v_f32m1(&C[ci], c1, gvl); 
            
            m_top += 4;
        }


        if( M & 2 ) {
#ifdef FP16_NARROW
            _Float16 result0 = 0;
            _Float16 result1 = 0;
            _Float16 result2 = 0;
            _Float16 result3 = 0;
#else
            float result0 = 0;
            float result1 = 0;
            float result2 = 0;
            float result3 = 0;
#endif
            BLASLONG ai = m_top * K;
            BLASLONG bi = n_top * K;

            for(BLASLONG k=0; k<K; k++) {
#ifdef FP16_NARROW
                result0+=A[ai+0]*B[bi+0];
                result1+=A[ai+1]*B[bi+0];
                result2+=A[ai+0]*B[bi+1];
                result3+=A[ai+1]*B[bi+1];
#else
                result0+=(float)(A[ai+0])*(float)(B[bi+0]);
                result1+=(float)(A[ai+1])*(float)(B[bi+0]);
                result2+=(float)(A[ai+0])*(float)(B[bi+1]);
                result3+=(float)(A[ai+1])*(float)(B[bi+1]);
#endif
                ai+=2;
                bi+=2;
            }
            
            BLASLONG ci=n_top*ldc+m_top;
#ifdef FP16_NARROW
            C[ci + 0 * ldc + 0] += alpha16 * (float)(result0);
            C[ci + 0 * ldc + 1] += alpha16 * (float)(result1);
            C[ci + 1 * ldc + 0] += alpha16 * (float)(result2);
            C[ci + 1 * ldc + 1] += alpha16 * (float)(result3);
#else
            C[ci + 0 * ldc + 0] += alpha * result0;
            C[ci + 0 * ldc + 1] += alpha * result1;
            C[ci + 1 * ldc + 0] += alpha * result2;
            C[ci + 1 * ldc + 1] += alpha * result3;
#endif

            m_top += 2;
        }


        if( M & 1 ) {
            
#ifdef FP16_NARROW
            _Float16 result0 = 0;
            _Float16 result1 = 0;
#else
            float result0 = 0;
            float result1 = 0;
#endif
            
            BLASLONG ai = m_top * K;
            BLASLONG bi = n_top * K;

            for(BLASLONG k=0; k<K; k++) {
#ifdef FP16_NARROW
                result0+=A[ai+0]*B[bi+0];
                result1+=A[ai+0]*B[bi+1];
#else
                result0+=(float)(A[ai+0])*(float)(B[bi+0]);
                result1+=(float)(A[ai+0])*(float)(B[bi+1]);
#endif
                ai+=1;
                bi+=2;
            }

            BLASLONG ci = n_top * ldc + m_top;
#ifdef FP16_NARROW
            C[ci + 0 * ldc + 0] += alpha16 * (float)(result0);
            C[ci + 1 * ldc + 0] += alpha16 * (float)(result1);
#else
            C[ci + 0 * ldc + 0] += alpha * result0;
            C[ci + 1 * ldc + 0] += alpha * result1;
#endif
            m_top += 1;
        }

        n_top += 2;
    }



    // -- tails for N=1
    if( N & 1 ) {
        m_top = 0;
#ifdef FP16_NARROW
        gvl = __riscv_vsetvl_e16m2(32);

        for (BLASLONG i=0; i<M/32; i+=1) {
            BLASLONG ai=m_top*K;
            BLASLONG bi=n_top*K;
            _Float16 B0 = B[bi+0];
            bi += 1;

            vfloat16m1_t A00 = __riscv_vle16_v_f16m1( &A[ai+0*gvl], 16 );
            vfloat16m1_t A01 = __riscv_vle16_v_f16m1( &A[ai+0*gvl+16*K], 16 );
            vfloat16m2_t A0 = __riscv_vcreate_v_f16m1_f16m2(A00, A01);
            ai += 16;

            vfloat16m2_t result0 = __riscv_vfmul_vf_f16m2( A0, B0, gvl);

            for(BLASLONG k=1; k<K; k++) {
                B0 = B[bi+0];
                bi += 1;

                A00 = __riscv_vle16_v_f16m1( &A[ai+0*gvl], 16 );
                A01 = __riscv_vle16_v_f16m1( &A[ai+0*gvl+16*K], 16 );
                A0 = __riscv_vcreate_v_f16m1_f16m2(A00, A01);
                ai += 16;

                result0 = __riscv_vfmacc_vf_f16m2(result0, B0, A0, gvl);
            }

            BLASLONG ci=n_top*ldc+m_top;

            vfloat32m4_t c0 = __riscv_vle32_v_f32m4( &C[ci], gvl);

            c0 = __riscv_vfwmacc_vf_f32m4(c0, alpha16, result0, gvl);

            ci=n_top*ldc+m_top;

            __riscv_vse32_v_f32m4( &C[ci], c0, gvl);
            m_top += 32;
        }

        if (M & 16) {
          gvl = __riscv_vsetvl_e16m1(16);
#else
        gvl = __riscv_vsetvl_e16m1(16);

        for (BLASLONG i=0; i<M/16; i+=1) {
#endif
            BLASLONG ai=m_top*K;	
            BLASLONG bi=n_top*K;	
            _Float16 B0 = B[bi+0];
            bi += 1;

            vfloat16m1_t A0 = __riscv_vle16_v_f16m1( &A[ai+0*gvl], gvl );
            ai += 16;

#ifdef FP16_NARROW
            vfloat16m1_t result0 = __riscv_vfmul_vf_f16m1( A0, B0, gvl);
#else
            vfloat32m2_t result0 = __riscv_vfwmul_vf_f32m2( A0, B0, gvl);
#endif

            for(BLASLONG k=1; k<K; k++) {
                B0 = B[bi+0];
                bi += 1;

                A0 = __riscv_vle16_v_f16m1( &A[ai+0*gvl], gvl );
                ai += 16;
                
#ifdef FP16_NARROW
                result0 = __riscv_vfmacc_vf_f16m1(result0, B0, A0, gvl);
#else
                result0 = __riscv_vfwmacc_vf_f32m2(result0, B0, A0, gvl);
#endif
            }
            
            BLASLONG ci=n_top*ldc+m_top;

            vfloat32m2_t c0 = __riscv_vle32_v_f32m2( &C[ci], gvl);
            
#ifdef FP16_NARROW
            c0 = __riscv_vfwmacc_vf_f32m2(c0, alpha16, result0, gvl);
#else
            c0 = __riscv_vfmacc_vf_f32m2(c0, alpha, result0, gvl);
#endif

            ci=n_top*ldc+m_top;

            __riscv_vse32_v_f32m2( &C[ci], c0, gvl);
            m_top += 16;
        }

        if( M & 8 ) {
            gvl = __riscv_vsetvl_e16mf2(8);
            BLASLONG ai=m_top*K;	
            BLASLONG bi=n_top*K;	
            
            _Float16 B0 = B[bi+0];
            bi += 1;

            vfloat16mf2_t A0 = __riscv_vle16_v_f16mf2( &A[ai+0*gvl], gvl );
            ai += 8;

#ifdef FP16_NARROW
            vfloat16mf2_t result0 = __riscv_vfmul_vf_f16mf2( A0, B0, gvl);
#else
            vfloat32m1_t result0 = __riscv_vfwmul_vf_f32m1( A0, B0, gvl);
#endif
		
            for(BLASLONG k=1; k<K; k++) {
                B0 = B[bi+0];
                bi += 1;

                A0 = __riscv_vle16_v_f16mf2( &A[ai+0*gvl], gvl );
                ai += 8;

#ifdef FP16_NARROW
                result0 = __riscv_vfmacc_vf_f16mf2(result0, B0, A0, gvl);
#else
                result0 = __riscv_vfwmacc_vf_f32m1(result0, B0, A0, gvl);
#endif
            }


            BLASLONG ci=n_top*ldc+m_top;

            vfloat32m1_t c0 = __riscv_vle32_v_f32m1( &C[ci], gvl);
            
#ifdef FP16_NARROW
            c0 = __riscv_vfwmacc_vf_f32m1(c0, alpha16, result0, gvl);
#else
            c0 = __riscv_vfmacc_vf_f32m1(c0, alpha, result0, gvl);
#endif

            ci = n_top * ldc + m_top;

            __riscv_vse32_v_f32m1( &C[ci], c0, gvl);
            m_top += 8;
        }

        if( M & 4 ) {
            gvl = __riscv_vsetvl_e16mf2(4);

            BLASLONG ai=m_top*K;
            BLASLONG bi=n_top*K;
            _Float16 B0 = B[bi+0];
            bi += 1;

            vfloat16mf2_t A0 = __riscv_vle16_v_f16mf2( &A[ai+0*gvl], gvl );
            ai += 4;

#ifdef FP16_NARROW
            vfloat16mf2_t result0 = __riscv_vfmul_vf_f16mf2( A0, B0, gvl);
#else
            vfloat32m1_t result0 = __riscv_vfwmul_vf_f32m1( A0, B0, gvl);
#endif

            for(BLASLONG k=1; k < K; ++k) {
                B0 = B[bi+0];
                bi += 1;

                A0 = __riscv_vle16_v_f16mf2( &A[ai+0*gvl], gvl );
                ai += 4;

#ifdef FP16_NARROW
                result0 = __riscv_vfmacc_vf_f16mf2(result0, B0, A0, gvl);
#else
                result0 = __riscv_vfwmacc_vf_f32m1(result0, B0, A0, gvl);
#endif
            }

            BLASLONG ci = n_top * ldc + m_top;

            vfloat32m1_t c0 = __riscv_vle32_v_f32m1(&C[ci], gvl);
#ifdef FP16_NARROW
            c0 = __riscv_vfwmacc_vf_f32m1(c0, alpha16, result0, gvl);
#else
            c0 = __riscv_vfmacc_vf_f32m1(c0, alpha, result0, gvl);
#endif

            ci= n_top * ldc + m_top;

            __riscv_vse32_v_f32m1(&C[ci], c0, gvl);            
            m_top += 4;
        }


        if( M & 2 ) {
#ifdef FP16_NARROW
            _Float16 result0 = 0;
            _Float16 result1 = 0;
#else
            float result0 = 0;
            float result1 = 0;
#endif
            BLASLONG ai = m_top * K;
            BLASLONG bi = n_top * K;

            for(BLASLONG k=0; k<K; k++) {
#ifdef FP16_NARROW
                result0+=A[ai+0]*B[bi+0];
                result1+=A[ai+1]*B[bi+0];
#else
                result0+=(float)(A[ai+0])*(float)(B[bi+0]);
                result1+=(float)(A[ai+1])*(float)(B[bi+0]);
#endif
                ai+=2;
                bi+=1;
            }
            
            
            BLASLONG ci=n_top*ldc+m_top;
#ifdef FP16_NARROW
            C[ci + 0 * ldc + 0] += alpha16 * (float)(result0);
            C[ci + 0 * ldc + 1] += alpha16 * (float)(result1);
#else
            C[ci + 0 * ldc + 0] += alpha * result0;
            C[ci + 0 * ldc + 1] += alpha * result1;
#endif

            m_top += 2;
        }


        if( M & 1 ) {
            
#ifdef FP16_NARROW
            _Float16 result0 = 0;
#else
            float result0 = 0;
#endif
            
            BLASLONG ai = m_top * K;
            BLASLONG bi = n_top * K;

            for(BLASLONG k=0; k<K; k++) {
#ifdef FP16_NARROW
                result0+=A[ai+0]*B[bi+0];
#else
                result0+=(float)(A[ai+0])*(float)(B[bi+0]);
#endif
                ai+=1;
                bi+=1;
            }

            BLASLONG ci = n_top * ldc + m_top;
#ifdef FP16_NARROW
            C[ci + 0 * ldc + 0] += alpha16 * (float)(result0);
#else
            C[ci + 0 * ldc + 0] += alpha * result0;
#endif
            m_top += 1;
        }

        n_top += 1;
    }
    return 0;
}
