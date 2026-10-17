/***************************************************************************
Copyright (c) 2026, The OpenBLAS Project
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

#include "common.h"
#include "../generic/conversion_macros.h"

#if defined(__wasm_simd128__)
#include <wasm_simd128.h>
#endif

#if defined(__wasm_simd128__)
#ifndef DOUBLE
static inline FLOAT hsum_vec(v128_t v) {
        return wasm_f32x4_extract_lane(v, 0) + wasm_f32x4_extract_lane(v, 1) +
               wasm_f32x4_extract_lane(v, 2) + wasm_f32x4_extract_lane(v, 3);
}
#else
static inline FLOAT hsum_vec(v128_t v) {
        return wasm_f64x2_extract_lane(v, 0) + wasm_f64x2_extract_lane(v, 1);
}
#endif
#endif

int CNAME(BLASLONG bm, BLASLONG bn, BLASLONG bk, FLOAT alpha, IFLOAT *ba,
          IFLOAT *bb, FLOAT *C, BLASLONG ldc
#ifdef TRMMKERNEL
          ,
          BLASLONG offset
#endif
)
{
        BLASLONG i, j, k;
        FLOAT *C0, *C1;
        IFLOAT *ptrba, *ptrbb;
#ifdef BGEMM
        float res0, res1, res2, res3;
#else
        FLOAT res0, res1, res2, res3;
#endif
        IFLOAT load0, load1, load2, load3, load4, load5, load6, load7;

        for (j = 0; j < bn / 2; j += 1) {
                C0 = C;
                C1 = C0 + ldc;
                ptrba = ba;

                for (i = 0; i < bm / 2; i += 1) {
                        ptrbb = bb;
                        res0 = 0;
                        res1 = 0;
                        res2 = 0;
                        res3 = 0;

#if defined(__wasm_simd128__) && !defined(BGEMM)
#ifndef DOUBLE
                        {
                                v128_t vacc00 = wasm_f32x4_splat(0.0f);
                                v128_t vacc10 = wasm_f32x4_splat(0.0f);
                                v128_t vacc01 = wasm_f32x4_splat(0.0f);
                                v128_t vacc11 = wasm_f32x4_splat(0.0f);

                                k = 0;
                                for (; k + 4 <= bk; k += 4) {
                                        v128_t va01 = wasm_v128_load(ptrba);
                                        v128_t va23 = wasm_v128_load(ptrba + 4);
                                        v128_t vb01 = wasm_v128_load(ptrbb);
                                        v128_t vb23 = wasm_v128_load(ptrbb + 4);

                                        v128_t vrow0 =
                                            wasm_i32x4_shuffle(va01, va23, 0, 2, 4, 6);
                                        v128_t vrow1 =
                                            wasm_i32x4_shuffle(va01, va23, 1, 3, 5, 7);
                                        v128_t vcol0 =
                                            wasm_i32x4_shuffle(vb01, vb23, 0, 2, 4, 6);
                                        v128_t vcol1 =
                                            wasm_i32x4_shuffle(vb01, vb23, 1, 3, 5, 7);

                                        vacc00 = wasm_f32x4_add(
                                            vacc00, wasm_f32x4_mul(vrow0, vcol0));
                                        vacc10 = wasm_f32x4_add(
                                            vacc10, wasm_f32x4_mul(vrow1, vcol0));
                                        vacc01 = wasm_f32x4_add(
                                            vacc01, wasm_f32x4_mul(vrow0, vcol1));
                                        vacc11 = wasm_f32x4_add(
                                            vacc11, wasm_f32x4_mul(vrow1, vcol1));

                                        ptrba += 8;
                                        ptrbb += 8;
                                }

                                res0 += hsum_vec(vacc00);
                                res1 += hsum_vec(vacc10);
                                res2 += hsum_vec(vacc01);
                                res3 += hsum_vec(vacc11);
                        }
#else
                        {
                                v128_t vacc00 = wasm_f64x2_splat(0.0);
                                v128_t vacc10 = wasm_f64x2_splat(0.0);
                                v128_t vacc01 = wasm_f64x2_splat(0.0);
                                v128_t vacc11 = wasm_f64x2_splat(0.0);

                                for (k = 0; k + 2 <= bk; k += 2) {
                                        v128_t va01 = wasm_v128_load(ptrba);
                                        v128_t va23 = wasm_v128_load(ptrba + 2);
                                        v128_t vb01 = wasm_v128_load(ptrbb);
                                        v128_t vb23 = wasm_v128_load(ptrbb + 2);

                                        v128_t vrow0 =
                                            wasm_i64x2_shuffle(va01, va23, 0, 2);
                                        v128_t vrow1 =
                                            wasm_i64x2_shuffle(va01, va23, 1, 3);
                                        v128_t vcol0 =
                                            wasm_i64x2_shuffle(vb01, vb23, 0, 2);
                                        v128_t vcol1 =
                                            wasm_i64x2_shuffle(vb01, vb23, 1, 3);

                                        vacc00 = wasm_f64x2_add(
                                            vacc00, wasm_f64x2_mul(vrow0, vcol0));
                                        vacc10 = wasm_f64x2_add(
                                            vacc10, wasm_f64x2_mul(vrow1, vcol0));
                                        vacc01 = wasm_f64x2_add(
                                            vacc01, wasm_f64x2_mul(vrow0, vcol1));
                                        vacc11 = wasm_f64x2_add(
                                            vacc11, wasm_f64x2_mul(vrow1, vcol1));

                                        ptrba += 4;
                                        ptrbb += 4;
                                }

                                res0 += hsum_vec(vacc00);
                                res1 += hsum_vec(vacc10);
                                res2 += hsum_vec(vacc01);
                                res3 += hsum_vec(vacc11);
                        }
#endif
#else
                        k = 0;
#endif

                        for (; k < bk; k += 1) {
                                load0 = ptrba[2 * 0 + 0];
                                load1 = ptrbb[2 * 0 + 0];
                                res0 = res0 + TO_F32(load0) * TO_F32(load1);
                                load2 = ptrba[2 * 0 + 1];
                                res1 = res1 + TO_F32(load2) * TO_F32(load1);
                                load3 = ptrbb[2 * 0 + 1];
                                res2 = res2 + TO_F32(load0) * TO_F32(load3);
                                res3 = res3 + TO_F32(load2) * TO_F32(load3);
                                ptrba = ptrba + 2;
                                ptrbb = ptrbb + 2;
                        }

                        res0 = res0 * ALPHA;
                        C0[0] = TO_OUTPUT(TO_F32(C0[0]) + res0);
                        res1 = res1 * ALPHA;
                        C0[1] = TO_OUTPUT(TO_F32(C0[1]) + res1);
                        res2 = res2 * ALPHA;
                        C1[0] = TO_OUTPUT(TO_F32(C1[0]) + res2);
                        res3 = res3 * ALPHA;
                        C1[1] = TO_OUTPUT(TO_F32(C1[1]) + res3);
                        C0 = C0 + 2;
                        C1 = C1 + 2;
                }

                for (i = 0; i < (bm & 1); i += 1) {
                        ptrbb = bb;
                        res0 = 0;
                        res1 = 0;
                        for (k = 0; k < bk; k += 1) {
                                load0 = ptrba[0 + 0];
                                load1 = ptrbb[2 * 0 + 0];
                                res0 = res0 + TO_F32(load0) * TO_F32(load1);
                                load2 = ptrbb[2 * 0 + 1];
                                res1 = res1 + TO_F32(load0) * TO_F32(load2);
                                ptrba = ptrba + 1;
                                ptrbb = ptrbb + 2;
                        }
                        res0 = res0 * ALPHA;
                        C0[0] = TO_OUTPUT(TO_F32(C0[0]) + res0);
                        res1 = res1 * ALPHA;
                        C1[0] = TO_OUTPUT(TO_F32(C1[0]) + res1);
                        C0 = C0 + 1;
                        C1 = C1 + 1;
                }

                k = (bk << 1);
                bb = bb + k;
                i = (ldc << 1);
                C = C + i;
        }

        for (j = 0; j < (bn & 1); j += 1) {
                C0 = C;
                ptrba = ba;
                for (i = 0; i < bm / 2; i += 1) {
                        ptrbb = bb;
                        res0 = 0;
                        res1 = 0;
                        for (k = 0; k < bk; k += 1) {
                                load0 = ptrba[2 * 0 + 0];
                                load1 = ptrbb[0 + 0];
                                res0 = res0 + TO_F32(load0) * TO_F32(load1);
                                load2 = ptrba[2 * 0 + 1];
                                res1 = res1 + TO_F32(load2) * TO_F32(load1);
                                ptrba = ptrba + 2;
                                ptrbb = ptrbb + 1;
                        }
                        res0 = res0 * ALPHA;
                        C0[0] = TO_OUTPUT(TO_F32(C0[0]) + res0);
                        res1 = res1 * ALPHA;
                        C0[1] = TO_OUTPUT(TO_F32(C0[1]) + res1);
                        C0 = C0 + 2;
                }

                for (i = 0; i < (bm & 1); i += 1) {
                        ptrbb = bb;
                        res0 = 0;
                        for (k = 0; k < bk; k += 1) {
                                load0 = ptrba[0 + 0];
                                load1 = ptrbb[0 + 0];
                                res0 = res0 + TO_F32(load0) * TO_F32(load1);
                                ptrba = ptrba + 1;
                                ptrbb = ptrbb + 1;
                        }
                        res0 = res0 * ALPHA;
                        C0[0] = TO_OUTPUT(TO_F32(C0[0]) + res0);
                        C0 = C0 + 1;
                }
                k = bk;
                bb = bb + k;
                i = ldc;
                C = C + i;
        }
        return 0;
}
