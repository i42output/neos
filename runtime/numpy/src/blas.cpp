/*
  blas.cpp

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

  Portions of this file are derived from NumPy 2.5.3 (numpy/_core/src/umath/matmul.c.src,
  numpy/_core/src/multiarray/arraytypes.c.src, numpy/_core/src/multiarray/vdot.c and
  numpy/_core/src/common/npy_cblas.h), Copyright (c) 2005-2025, NumPy Developers; All rights reserved. They are used
  under NumPy's BSD 3-Clause licence (see runtime/numpy/LICENSE.NumPy). Changes: translated from C to C++ for the neos
  NumPy runtime plugin and adapted to its object model (see runtime/numpy/NOTICE).
*/

// blas.cpp — NumPy's loops that use BLAS (OpenBLAS), as NumPy's (arraytypes.c.src, vdot.c, matmul.c.src)

#include <cblas.h>

#include <cstdlib>
#include <cstring>
#include <limits>
#include <type_traits>

#include "blas.hpp"
#include "cast.hpp"

namespace neos::numpy::blas
{
    namespace
    {
        using cint = blasint;

        // (BLAS_MAXSIZE: 64-bit BLAS integers)
        constexpr std::int64_t kBlasMaxSize = std::numeric_limits<std::int64_t>::max() - 1;

        // a 2-D matrix BLAS can use (is_blasable2d): the second axis contiguous; the first's stride, in elements, at least
        // the second's dimension
        bool blasable2d(std::int64_t aByteStride1, std::int64_t aByteStride2, std::int64_t aD1, std::int64_t aD2, std::int64_t aItemsize)
        {
            (void)aD1;
            std::int64_t const unitStride1 = aByteStride1 / aItemsize;
            if (aByteStride2 != aItemsize)
                return false;
            return aByteStride1 % aItemsize == 0 && unitStride1 >= aD2 && unitStride1 <= kBlasMaxSize;
        }

        // a NumPy stride (bytes) as a BLAS stride (elements); zero if it cannot be one (blas_stride)
        cint blas_stride(std::int64_t aStride, std::int64_t aItemsize)
        {
            if (aStride > 0 && aStride % aItemsize == 0)
            {
                aStride /= aItemsize;
                if (aStride <= std::numeric_limits<cint>::max())
                    return static_cast<cint>(aStride);
            }
            return 0;
        }

        template <typename T> constexpr bool is_complex_blas_v = std::is_same_v<T, std::complex<float>> || std::is_same_v<T, std::complex<double>>;
        template <typename T> constexpr bool is_blas_v = std::is_same_v<T, float> || std::is_same_v<T, double> || is_complex_blas_v<T>;

        template <typename T> using real_t = typename std::conditional_t<is_complex_v<T>, T, std::complex<T>>::value_type;

        template <typename T> T const* one()
        {
            static T const sOne{ 1 };
            return &sOne;
        }
        template <typename T> T const* zero()
        {
            static T const sZero{ 0 };
            return &sZero;
        }

        template <typename T> real_t<T>* parts(unsigned char* aPointer) { return reinterpret_cast<real_t<T>*>(aPointer); }

        // --- dot functions

        template <typename T>
        void typed_dot(unsigned char* ip1, std::int64_t is1, unsigned char* ip2, std::int64_t is2, unsigned char* op, std::int64_t n)
        {
            if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>)
            {
                auto const is1b = blas_stride(is1, sizeof(T));
                auto const is2b = blas_stride(is2, sizeof(T));
                if (is1b && is2b)
                {
                    double sum = 0.0;
                    if constexpr (std::is_same_v<T, float>)
                        sum += cblas_sdot(static_cast<cint>(n), reinterpret_cast<float const*>(ip1), is1b, reinterpret_cast<float const*>(ip2), is2b);
                    else
                        sum += cblas_ddot(static_cast<cint>(n), reinterpret_cast<double const*>(ip1), is1b, reinterpret_cast<double const*>(ip2), is2b);
                    *reinterpret_cast<T*>(op) = static_cast<T>(sum);
                }
                else
                {
                    T sum = 0;
                    for (std::int64_t i = 0; i < n; ++i, ip1 += is1, ip2 += is2)
                        sum += *reinterpret_cast<T const*>(ip1) * *reinterpret_cast<T const*>(ip2);
                    *reinterpret_cast<T*>(op) = sum;
                }
            }
            else if constexpr (is_complex_blas_v<T>)
            {
                using R = real_t<T>;
                auto const is1b = blas_stride(is1, sizeof(T));
                auto const is2b = blas_stride(is2, sizeof(T));
                if (is1b && is2b)
                {
                    double sum[2] = { 0.0, 0.0 };
                    R tmp[2];
                    if constexpr (std::is_same_v<R, float>)
                        cblas_cdotu_sub(static_cast<cint>(n), ip1, is1b, ip2, is2b, tmp);
                    else
                        cblas_zdotu_sub(static_cast<cint>(n), ip1, is1b, ip2, is2b, tmp);
                    sum[0] += static_cast<double>(tmp[0]);
                    sum[1] += static_cast<double>(tmp[1]);
                    parts<T>(op)[0] = static_cast<R>(sum[0]);
                    parts<T>(op)[1] = static_cast<R>(sum[1]);
                }
                else
                {
                    R sumr = 0, sumi = 0;
                    for (std::int64_t i = 0; i < n; ++i, ip1 += is1, ip2 += is2)
                    {
                        R const ip1r = parts<T>(ip1)[0], ip1i = parts<T>(ip1)[1];
                        R const ip2r = parts<T>(ip2)[0], ip2i = parts<T>(ip2)[1];
                        sumr += ip1r * ip2r - ip1i * ip2i;
                        sumi += ip1r * ip2i + ip1i * ip2r;
                    }
                    parts<T>(op)[0] = sumr;
                    parts<T>(op)[1] = sumi;
                }
            }
            else if constexpr (is_half_v<T>)
            {
                float tmp = 0.0f;
                for (std::int64_t i = 0; i < n; ++i, ip1 += is1, ip2 += is2)
                    tmp += static_cast<float>(*reinterpret_cast<half const*>(ip1)) * static_cast<float>(*reinterpret_cast<half const*>(ip2));
                *reinterpret_cast<half*>(op) = half{ tmp };
            }
            else if constexpr (is_bool_v<T>)
            {
                bool tmp = false;
                for (std::int64_t i = 0; i < n; ++i, ip1 += is1, ip2 += is2)
                    if (*ip1 != 0 && *ip2 != 0)
                    {
                        tmp = true;
                        break;
                    }
                *reinterpret_cast<bool*>(op) = tmp;
            }
            else
            {
                // (integers: the sum in 64 bits (npy_long, npy_longlong), wrapping, then the type's)
                std::uint64_t tmp = 0u;
                for (std::int64_t i = 0; i < n; ++i, ip1 += is1, ip2 += is2)
                    tmp += static_cast<std::uint64_t>(*reinterpret_cast<T const*>(ip1)) * static_cast<std::uint64_t>(*reinterpret_cast<T const*>(ip2));
                *reinterpret_cast<T*>(op) = static_cast<T>(tmp);
            }
        }

        // (the first operand conjugated; aSumDouble: TYPE_dotc's sums (double for complex64) rather than TYPE_vdot's)
        template <typename T, bool SumDouble>
        void typed_dotc(unsigned char* ip1, std::int64_t is1, unsigned char* ip2, std::int64_t is2, unsigned char* op, std::int64_t n)
        {
            using R = real_t<T>;
            auto const is1b = blas_stride(is1, sizeof(T));
            auto const is2b = blas_stride(is2, sizeof(T));
            if (is1b && is2b)
            {
                double sum[2] = { 0.0, 0.0 };
                R tmp[2];
                if constexpr (std::is_same_v<R, float>)
                    cblas_cdotc_sub(static_cast<cint>(n), ip1, is1b, ip2, is2b, tmp);
                else
                    cblas_zdotc_sub(static_cast<cint>(n), ip1, is1b, ip2, is2b, tmp);
                sum[0] += static_cast<double>(tmp[0]);
                sum[1] += static_cast<double>(tmp[1]);
                parts<T>(op)[0] = static_cast<R>(sum[0]);
                parts<T>(op)[1] = static_cast<R>(sum[1]);
            }
            else
            {
                using S = std::conditional_t<SumDouble, double, R>;
                S sumr = 0, sumi = 0;
                for (std::int64_t i = 0; i < n; ++i, ip1 += is1, ip2 += is2)
                {
                    R const ip1r = parts<T>(ip1)[0], ip1i = parts<T>(ip1)[1];
                    R const ip2r = parts<T>(ip2)[0], ip2i = parts<T>(ip2)[1];
                    sumr += static_cast<S>(ip1r * ip2r + ip1i * ip2i);
                    sumi += static_cast<S>(ip1r * ip2i - ip1i * ip2r);
                }
                parts<T>(op)[0] = static_cast<R>(sumr);
                parts<T>(op)[1] = static_cast<R>(sumi);
            }
        }

        // --- matmul

        template <typename T>
        void typed_gemv(unsigned char* ip1, std::int64_t is1_m, std::int64_t is1_n, unsigned char* ip2, std::int64_t is2_n,
            unsigned char* op, std::int64_t op_m, std::int64_t m, std::int64_t n)
        {
            constexpr std::int64_t sz = sizeof(T);
            CBLAS_ORDER order;
            cint lda;
            if (blasable2d(is1_m, is1_n, m, n, sz))
            {
                order = CblasColMajor;
                lda = static_cast<cint>(is1_m / sz);
            }
            else
            {
                order = CblasRowMajor;
                lda = static_cast<cint>(is1_n / sz);
            }
            auto const M = static_cast<cint>(m), N = static_cast<cint>(n);
            auto const incX = static_cast<cint>(is2_n / sz), incY = static_cast<cint>(op_m / sz);
            if constexpr (std::is_same_v<T, float>)
                cblas_sgemv(order, CblasTrans, N, M, 1.0f, reinterpret_cast<float const*>(ip1), lda, reinterpret_cast<float const*>(ip2), incX, 0.0f, reinterpret_cast<float*>(op), incY);
            else if constexpr (std::is_same_v<T, double>)
                cblas_dgemv(order, CblasTrans, N, M, 1.0, reinterpret_cast<double const*>(ip1), lda, reinterpret_cast<double const*>(ip2), incX, 0.0, reinterpret_cast<double*>(op), incY);
            else if constexpr (std::is_same_v<T, std::complex<float>>)
                cblas_cgemv(order, CblasTrans, N, M, one<T>(), ip1, lda, ip2, incX, zero<T>(), op, incY);
            else
                cblas_zgemv(order, CblasTrans, N, M, one<T>(), ip1, lda, ip2, incX, zero<T>(), op, incY);
        }

        template <typename T>
        void typed_gemm(CBLAS_ORDER aOrder, CBLAS_TRANSPOSE aTransA, CBLAS_TRANSPOSE aTransB, cint aM, cint aN, cint aK,
            void const* aA, cint aLda, void const* aB, cint aLdb, void* aC, cint aLdc)
        {
            if constexpr (std::is_same_v<T, float>)
                cblas_sgemm(aOrder, aTransA, aTransB, aM, aN, aK, 1.0f, static_cast<float const*>(aA), aLda, static_cast<float const*>(aB), aLdb, 0.0f, static_cast<float*>(aC), aLdc);
            else if constexpr (std::is_same_v<T, double>)
                cblas_dgemm(aOrder, aTransA, aTransB, aM, aN, aK, 1.0, static_cast<double const*>(aA), aLda, static_cast<double const*>(aB), aLdb, 0.0, static_cast<double*>(aC), aLdc);
            else if constexpr (std::is_same_v<T, std::complex<float>>)
                cblas_cgemm(aOrder, aTransA, aTransB, aM, aN, aK, one<T>(), aA, aLda, aB, aLdb, zero<T>(), aC, aLdc);
            else
                cblas_zgemm(aOrder, aTransA, aTransB, aM, aN, aK, one<T>(), aA, aLda, aB, aLdb, zero<T>(), aC, aLdc);
        }

        template <typename T>
        void typed_syrk(CBLAS_ORDER aOrder, CBLAS_TRANSPOSE aTrans, cint aN, cint aK, void const* aA, cint aLda, void* aC, cint aLdc)
        {
            if constexpr (std::is_same_v<T, float>)
                cblas_ssyrk(aOrder, CblasUpper, aTrans, aN, aK, 1.0f, static_cast<float const*>(aA), aLda, 0.0f, static_cast<float*>(aC), aLdc);
            else if constexpr (std::is_same_v<T, double>)
                cblas_dsyrk(aOrder, CblasUpper, aTrans, aN, aK, 1.0, static_cast<double const*>(aA), aLda, 0.0, static_cast<double*>(aC), aLdc);
            else if constexpr (std::is_same_v<T, std::complex<float>>)
                cblas_csyrk(aOrder, CblasUpper, aTrans, aN, aK, one<T>(), aA, aLda, zero<T>(), aC, aLdc);
            else
                cblas_zsyrk(aOrder, CblasUpper, aTrans, aN, aK, one<T>(), aA, aLda, zero<T>(), aC, aLdc);
        }

        template <typename T>
        void matmul_matrixmatrix(unsigned char* ip1, std::int64_t is1_m, std::int64_t is1_n, unsigned char* ip2, std::int64_t is2_n, std::int64_t is2_p,
            unsigned char* op, std::int64_t os_m, std::int64_t os_p, std::int64_t m, std::int64_t n, std::int64_t p)
        {
            constexpr std::int64_t sz = sizeof(T);
            (void)os_p;
            CBLAS_ORDER const order = CblasRowMajor;
            CBLAS_TRANSPOSE trans1, trans2;
            cint lda, ldb;
            auto const M = static_cast<cint>(m), N = static_cast<cint>(n), P = static_cast<cint>(p);
            auto const ldc = static_cast<cint>(os_m / sz);
            if (blasable2d(is1_m, is1_n, m, n, sz))
            {
                trans1 = CblasNoTrans;
                lda = static_cast<cint>(is1_m / sz);
            }
            else
            {
                trans1 = CblasTrans;
                lda = static_cast<cint>(is1_n / sz);
            }
            if (blasable2d(is2_n, is2_p, n, p, sz))
            {
                trans2 = CblasNoTrans;
                ldb = static_cast<cint>(is2_n / sz);
            }
            else
            {
                trans2 = CblasTrans;
                ldb = static_cast<cint>(is2_p / sz);
            }
            // (a matrix times its transpose: syrk)
            if (ip1 == ip2 && m == p && is1_m == is2_p && is1_n == is2_n && trans1 != trans2)
            {
                if (trans1 == CblasNoTrans)
                    typed_syrk<T>(order, trans1, P, N, ip1, lda, op, ldc);
                else
                    typed_syrk<T>(order, trans1, P, N, ip1, ldb, op, ldc);
                // (the triangle copied)
                auto* const o = reinterpret_cast<T*>(op);
                for (cint i = 0; i < P; ++i)
                    for (cint j = i + 1; j < P; ++j)
                        o[j * ldc + i] = o[i * ldc + j];
            }
            else
                typed_gemm<T>(order, trans1, trans2, M, P, N, ip1, lda, ip2, ldb, op, ldc);
        }

        template <typename T>
        void matrix_copy(bool aTranspose, unsigned char* ip, std::int64_t is_m, std::int64_t is_n, unsigned char* op, std::int64_t os_m, std::int64_t os_n,
            std::int64_t dm, std::int64_t dn)
        {
            if (aTranspose)
            {
                auto const ib = is_m * dm, ob = os_m * dm;
                for (std::int64_t n = 0; n < dn; ++n)
                {
                    for (std::int64_t m = 0; m < dm; ++m)
                    {
                        std::memcpy(op, ip, sizeof(T));
                        ip += is_m;
                        op += os_m;
                    }
                    ip += is_n - ib;
                    op += os_n - ob;
                }
                return;
            }
            auto const ib = is_n * dn, ob = os_n * dn;
            for (std::int64_t m = 0; m < dm; ++m)
            {
                for (std::int64_t n = 0; n < dn; ++n)
                {
                    std::memcpy(op, ip, sizeof(T));
                    ip += is_n;
                    op += os_n;
                }
                ip += is_m - ib;
                op += os_m - ob;
            }
        }

        template <typename T>
        void matmul_inner_noblas(unsigned char* ip1, std::int64_t is1_m, std::int64_t is1_n, unsigned char* ip2, std::int64_t is2_n, std::int64_t is2_p,
            unsigned char* op, std::int64_t os_m, std::int64_t os_p, std::int64_t dm, std::int64_t dn, std::int64_t dp)
        {
            if constexpr (is_bool_v<T>)
            {
                auto const ib2_p = is2_p * dp, ob_p = os_p * dp;
                for (std::int64_t m = 0; m < dm; ++m)
                {
                    for (std::int64_t p = 0; p < dp; ++p)
                    {
                        auto* ip1tmp = ip1;
                        auto* ip2tmp = ip2;
                        *reinterpret_cast<bool*>(op) = false;
                        for (std::int64_t n = 0; n < dn; ++n)
                        {
                            if (*ip1tmp != 0 && *ip2tmp != 0)
                            {
                                *reinterpret_cast<bool*>(op) = true;
                                break;
                            }
                            ip2tmp += is2_n;
                            ip1tmp += is1_n;
                        }
                        op += os_p;
                        ip2 += is2_p;
                    }
                    op -= ob_p;
                    ip2 -= ib2_p;
                    ip1 += is1_m;
                    op += os_m;
                }
                return;
            }
            auto const ib1_n = is1_n * dn, ib2_n = is2_n * dn, ib2_p = is2_p * dp, ob_p = os_p * dp;
            for (std::int64_t m = 0; m < dm; ++m)
            {
                for (std::int64_t p = 0; p < dp; ++p)
                {
                    if constexpr (is_complex_v<T>)
                    {
                        using R = real_t<T>;
                        auto* const o = parts<T>(op);
                        o[0] = 0;
                        o[1] = 0;
                        for (std::int64_t n = 0; n < dn; ++n)
                        {
                            R const r1 = parts<T>(ip1)[0], i1 = parts<T>(ip1)[1];
                            R const r2 = parts<T>(ip2)[0], i2 = parts<T>(ip2)[1];
                            o[0] = o[0] + ((r1 * r2) - (i1 * i2));
                            o[1] = o[1] + ((r1 * i2) + (i1 * r2));
                            ip2 += is2_n;
                            ip1 += is1_n;
                        }
                    }
                    else if constexpr (is_half_v<T>)
                    {
                        float sum = 0;
                        for (std::int64_t n = 0; n < dn; ++n)
                        {
                            sum += static_cast<float>(*reinterpret_cast<half const*>(ip1)) * static_cast<float>(*reinterpret_cast<half const*>(ip2));
                            ip2 += is2_n;
                            ip1 += is1_n;
                        }
                        *reinterpret_cast<half*>(op) = half{ sum };
                    }
                    else if constexpr (std::is_floating_point_v<T>)
                    {
                        auto& o = *reinterpret_cast<T*>(op);
                        o = 0;
                        for (std::int64_t n = 0; n < dn; ++n)
                        {
                            o += *reinterpret_cast<T const*>(ip1) * *reinterpret_cast<T const*>(ip2);
                            ip2 += is2_n;
                            ip1 += is1_n;
                        }
                    }
                    else
                    {
                        // (integers: wrapping)
                        auto& o = *reinterpret_cast<T*>(op);
                        o = 0;
                        for (std::int64_t n = 0; n < dn; ++n)
                        {
                            o = static_cast<T>(static_cast<std::uint64_t>(o) +
                                static_cast<std::uint64_t>(*reinterpret_cast<T const*>(ip1)) * static_cast<std::uint64_t>(*reinterpret_cast<T const*>(ip2)));
                            ip2 += is2_n;
                            ip1 += is1_n;
                        }
                    }
                    ip1 -= ib1_n;
                    ip2 -= ib2_n;
                    op += os_p;
                    ip2 += is2_p;
                }
                op -= ob_p;
                ip2 -= ib2_p;
                ip1 += is1_m;
                op += os_m;
            }
        }

        template <typename T>
        void typed_matmul(std::int64_t dOuter, unsigned char** args, std::int64_t const* outerSteps, std::int64_t dm, std::int64_t dn, std::int64_t dp, std::int64_t const* steps)
        {
            auto const s0 = outerSteps[0], s1 = outerSteps[1], s2 = outerSteps[2];
            auto const is1_m = steps[0], is1_n = steps[1], is2_n = steps[2], is2_p = steps[3], os_m = steps[4], os_p = steps[5];
            unsigned char* a0 = args[0];
            unsigned char* a1 = args[1];
            unsigned char* a2 = args[2];
            if constexpr (!is_blas_v<T>)
            {
                for (std::int64_t iOuter = 0; iOuter < dOuter; ++iOuter, a0 += s0, a1 += s1, a2 += s2)
                    matmul_inner_noblas<T>(a0, is1_m, is1_n, a1, is2_n, is2_p, a2, os_m, os_p, dm, dn, dp);
            }
            else
            {
                constexpr std::int64_t sz = sizeof(T);
                bool const special_case = dm == 1 || dn == 1 || dp == 1;
                bool const any_zero_dim = dm == 0 || dn == 0 || dp == 0;
                bool const scalar_out = dm == 1 && dp == 1;
                bool const scalar_vec = dn == 1 && (dp == 1 || dm == 1);
                bool const too_big_for_blas = dm > kBlasMaxSize || dn > kBlasMaxSize || dp > kBlasMaxSize;
                bool const i1_c_blasable = blasable2d(is1_m, is1_n, dm, dn, sz);
                bool const i2_c_blasable = blasable2d(is2_n, is2_p, dn, dp, sz);
                bool const i1_f_blasable = blasable2d(is1_n, is1_m, dn, dm, sz);
                bool const i2_f_blasable = blasable2d(is2_p, is2_n, dp, dn, sz);
                bool const i1blasable = i1_c_blasable || i1_f_blasable;
                bool const i2blasable = i2_c_blasable || i2_f_blasable;
                bool const o_c_blasable = blasable2d(os_m, os_p, dm, dp, sz);
                bool const o_f_blasable = blasable2d(os_p, os_m, dp, dm, sz);
                bool const oblasable = o_c_blasable || o_f_blasable;
                bool const vector_matrix = dm == 1 && i2blasable && blasable2d(is1_n, sz, dn, 1, sz);
                bool const matrix_vector = dp == 1 && i1blasable && blasable2d(is2_n, sz, dn, 1, sz);
                bool const noblas_fallback = too_big_for_blas || any_zero_dim;
                bool const matrix_matrix = !noblas_fallback && !special_case;
                bool const allocate_buffer = matrix_matrix && (!i1blasable || !i2blasable || !oblasable);
                std::unique_ptr<unsigned char[]> buffer;
                unsigned char* tmp_ip1 = nullptr;
                unsigned char* tmp_ip2 = nullptr;
                unsigned char* tmp_op = nullptr;
                if (allocate_buffer)
                {
                    std::int64_t const ip1_size = i1blasable ? 0 : sz * dm * dn;
                    std::int64_t const ip2_size = i2blasable ? 0 : sz * dn * dp;
                    std::int64_t const op_size = oblasable ? 0 : sz * dm * dp;
                    buffer.reset(new unsigned char[static_cast<std::size_t>(ip1_size + ip2_size + op_size)]);
                    tmp_ip1 = buffer.get();
                    tmp_ip2 = buffer.get() + ip1_size;
                    tmp_op = buffer.get() + ip1_size + ip2_size;
                }
                for (std::int64_t iOuter = 0; iOuter < dOuter; ++iOuter, a0 += s0, a1 += s1, a2 += s2)
                {
                    unsigned char* const ip1 = a0;
                    unsigned char* const ip2 = a1;
                    unsigned char* const op = a2;
                    if (noblas_fallback)
                        matmul_inner_noblas<T>(ip1, is1_m, is1_n, ip2, is2_n, is2_p, op, os_m, os_p, dm, dn, dp);
                    else if (special_case)
                    {
                        if (scalar_out)
                            typed_dot<T>(ip1, is1_n, ip2, is2_n, op, dn);
                        else if (scalar_vec)
                            matmul_inner_noblas<T>(ip1, is1_m, is1_n, ip2, is2_n, is2_p, op, os_m, os_p, dm, dn, dp);
                        else if (vector_matrix)
                            typed_gemv<T>(ip2, is2_p, is2_n, ip1, is1_n, op, os_p, dp, dn);
                        else if (matrix_vector)
                            typed_gemv<T>(ip1, is1_m, is1_n, ip2, is2_n, op, os_m, dm, dn);
                        else
                            matmul_inner_noblas<T>(ip1, is1_m, is1_n, ip2, is2_n, is2_p, op, os_m, os_p, dm, dn, dp);
                    }
                    else
                    {
                        // (copies of the matrices BLAS cannot use)
                        bool const i1_transpose = std::abs(is1_m) < std::abs(is1_n);
                        bool const i2_transpose = std::abs(is2_n) < std::abs(is2_p);
                        bool const o_transpose = std::abs(os_m) < std::abs(os_p);
                        std::int64_t const tmp_is1_m = i1_transpose ? sz : sz * dn, tmp_is1_n = i1_transpose ? sz * dm : sz;
                        std::int64_t const tmp_is2_n = i2_transpose ? sz : sz * dp, tmp_is2_p = i2_transpose ? sz * dn : sz;
                        std::int64_t const tmp_os_m = o_transpose ? sz : sz * dp, tmp_os_p = o_transpose ? sz * dm : sz;
                        if (!i1blasable)
                            matrix_copy<T>(i1_transpose, ip1, is1_m, is1_n, tmp_ip1, tmp_is1_m, tmp_is1_n, dm, dn);
                        if (!i2blasable)
                            matrix_copy<T>(i2_transpose, ip2, is2_n, is2_p, tmp_ip2, tmp_is2_n, tmp_is2_p, dn, dp);
                        auto* const ip1_ = i1blasable ? ip1 : tmp_ip1;
                        auto* const ip2_ = i2blasable ? ip2 : tmp_ip2;
                        auto* const op_ = oblasable ? op : tmp_op;
                        auto const is1_m_ = i1blasable ? is1_m : tmp_is1_m, is1_n_ = i1blasable ? is1_n : tmp_is1_n;
                        auto const is2_n_ = i2blasable ? is2_n : tmp_is2_n, is2_p_ = i2blasable ? is2_p : tmp_is2_p;
                        auto const os_m_ = oblasable ? os_m : tmp_os_m, os_p_ = oblasable ? os_p : tmp_os_p;
                        // (matmul(a, b, o) == matmul(b.T, a.T, o.T))
                        if (o_transpose)
                            matmul_matrixmatrix<T>(ip2_, is2_p_, is2_n_, ip1_, is1_n_, is1_m_, op_, os_p_, os_m_, dp, dn, dm);
                        else
                            matmul_matrixmatrix<T>(ip1_, is1_m_, is1_n_, ip2_, is2_n_, is2_p_, op_, os_m_, os_p_, dm, dn, dp);
                        if (!oblasable)
                            matrix_copy<T>(o_transpose, tmp_op, tmp_os_m, tmp_os_p, op, os_m, os_p, dm, dp);
                    }
                }
            }
        }

        // (vecmat for complex: gemm, which conjugates the vector)
        template <typename T>
        void vecmat_via_gemm(unsigned char* ip1, std::int64_t is1_n, unsigned char* ip2, std::int64_t is2_n, std::int64_t is2_m, unsigned char* op,
            std::int64_t n, std::int64_t m)
        {
            constexpr std::int64_t sz = sizeof(T);
            CBLAS_TRANSPOSE trans2;
            cint ldb;
            auto const N = static_cast<cint>(n), M = static_cast<cint>(m);
            auto const ldc = M;
            auto const lda = static_cast<cint>(is1_n / sz);
            if (blasable2d(is2_n, is2_m, n, m, sz))
            {
                trans2 = CblasNoTrans;
                ldb = static_cast<cint>(is2_n / sz);
            }
            else
            {
                trans2 = CblasTrans;
                ldb = static_cast<cint>(is2_m / sz);
            }
            if constexpr (std::is_same_v<T, std::complex<float>>)
                cblas_cgemm(CblasRowMajor, CblasConjTrans, trans2, 1, M, N, one<T>(), ip1, lda, ip2, ldb, zero<T>(), op, ldc);
            else
                cblas_zgemm(CblasRowMajor, CblasConjTrans, trans2, 1, M, N, one<T>(), ip1, lda, ip2, ldb, zero<T>(), op, ldc);
        }

        template <typename F>
        decltype(auto) dispatch_type(dtype aType, F&& f)
        {
            return dispatch(aType, std::forward<F>(f));
        }
    }

    bool is_blas_type(dtype aType)
    {
        return aType == dtype::Float32 || aType == dtype::Float64 || aType == dtype::Complex64 || aType == dtype::Complex128;
    }

    void dot(dtype aType, unsigned char* aIp1, std::int64_t aIs1, unsigned char* aIp2, std::int64_t aIs2, unsigned char* aOp, std::int64_t aN)
    {
        dispatch_type(aType, [&](auto tag) { typed_dot<decltype(tag)>(aIp1, aIs1, aIp2, aIs2, aOp, aN); });
    }

    void vdot(dtype aType, unsigned char* aIp1, std::int64_t aIs1, unsigned char* aIp2, std::int64_t aIs2, unsigned char* aOp, std::int64_t aN)
    {
        if (aType == dtype::Complex64)
            typed_dotc<std::complex<float>, false>(aIp1, aIs1, aIp2, aIs2, aOp, aN);
        else if (aType == dtype::Complex128)
            typed_dotc<std::complex<double>, false>(aIp1, aIs1, aIp2, aIs2, aOp, aN);
        else
            dot(aType, aIp1, aIs1, aIp2, aIs2, aOp, aN);
    }

    void dotc(dtype aType, unsigned char* aIp1, std::int64_t aIs1, unsigned char* aIp2, std::int64_t aIs2, unsigned char* aOp, std::int64_t aN)
    {
        if (aType == dtype::Complex64)
            typed_dotc<std::complex<float>, true>(aIp1, aIs1, aIp2, aIs2, aOp, aN);
        else if (aType == dtype::Complex128)
            typed_dotc<std::complex<double>, true>(aIp1, aIs1, aIp2, aIs2, aOp, aN);
        else
            dot(aType, aIp1, aIs1, aIp2, aIs2, aOp, aN);
    }

    void matmul(dtype aType, std::int64_t aOuter, unsigned char** aArgs, std::int64_t const* aOuterSteps, std::int64_t aDm, std::int64_t aDn, std::int64_t aDp, std::int64_t const* aSteps)
    {
        dispatch_type(aType, [&](auto tag) { typed_matmul<decltype(tag)>(aOuter, aArgs, aOuterSteps, aDm, aDn, aDp, aSteps); });
    }

    void vecdot(dtype aType, std::int64_t aOuter, unsigned char** aArgs, std::int64_t const* aOuterSteps, std::int64_t aDn, std::int64_t aIs1, std::int64_t aIs2)
    {
        unsigned char* ip1 = aArgs[0];
        unsigned char* ip2 = aArgs[1];
        unsigned char* op = aArgs[2];
        for (std::int64_t i = 0; i < aOuter; ++i, ip1 += aOuterSteps[0], ip2 += aOuterSteps[1], op += aOuterSteps[2])
            dotc(aType, ip1, aIs1, ip2, aIs2, op, aDn);
    }

    void matvec(dtype aType, std::int64_t aOuter, unsigned char** aArgs, std::int64_t const* aOuterSteps, std::int64_t aDm, std::int64_t aDn, std::int64_t const* aSteps)
    {
        auto const is1_m = aSteps[0], is1_n = aSteps[1], is2_n = aSteps[2], os_m = aSteps[3];
        dispatch_type(aType, [&](auto tag)
        {
            using T = decltype(tag);
            bool blasable = false;
            if constexpr (is_blas_v<T>)
            {
                constexpr std::int64_t sz = sizeof(T);
                bool const too_big_for_blas = aDm > kBlasMaxSize || aDn > kBlasMaxSize;
                bool const i1_c_blasable = blasable2d(is1_m, is1_n, aDm, aDn, sz);
                bool const i1_f_blasable = blasable2d(is1_n, is1_m, aDn, aDm, sz);
                bool const i2_blasable = blasable2d(is2_n, sz, aDn, 1, sz);
                blasable = (i1_c_blasable || i1_f_blasable) && i2_blasable && !too_big_for_blas && aDn > 1 && aDm > 1;
            }
            unsigned char* a0 = aArgs[0];
            unsigned char* a1 = aArgs[1];
            unsigned char* a2 = aArgs[2];
            for (std::int64_t i = 0; i < aOuter; ++i, a0 += aOuterSteps[0], a1 += aOuterSteps[1], a2 += aOuterSteps[2])
            {
                if constexpr (is_blas_v<T>)
                    if (blasable)
                    {
                        typed_gemv<T>(a0, is1_m, is1_n, a1, is2_n, a2, os_m, aDm, aDn);
                        continue;
                    }
                // (no conjugation)
                unsigned char* ip1 = a0;
                unsigned char* op = a2;
                for (std::int64_t j = 0; j < aDm; ++j, ip1 += is1_m, op += os_m)
                    typed_dot<T>(ip1, is1_n, a1, is2_n, op, aDn);
            }
        });
    }

    void vecmat(dtype aType, std::int64_t aOuter, unsigned char** aArgs, std::int64_t const* aOuterSteps, std::int64_t aDn, std::int64_t aDm, std::int64_t const* aSteps)
    {
        auto const is1_n = aSteps[0], is2_n = aSteps[1], is2_m = aSteps[2], os_m = aSteps[3];
        dispatch_type(aType, [&](auto tag)
        {
            using T = decltype(tag);
            bool blasable = false;
            if constexpr (is_blas_v<T>)
            {
                constexpr std::int64_t sz = sizeof(T);
                bool const too_big_for_blas = aDm > kBlasMaxSize || aDn > kBlasMaxSize;
                bool const i1_blasable = blasable2d(is1_n, sz, aDn, 1, sz);
                bool const i2_c_blasable = blasable2d(is2_n, is2_m, aDn, aDm, sz);
                bool const i2_f_blasable = blasable2d(is2_m, is2_n, aDm, aDn, sz);
                blasable = i1_blasable && (i2_c_blasable || i2_f_blasable) && !too_big_for_blas && aDn > 1 && aDm > 1;
            }
            unsigned char* a0 = aArgs[0];
            unsigned char* a1 = aArgs[1];
            unsigned char* a2 = aArgs[2];
            for (std::int64_t i = 0; i < aOuter; ++i, a0 += aOuterSteps[0], a1 += aOuterSteps[1], a2 += aOuterSteps[2])
            {
                if constexpr (is_blas_v<T>)
                    if (blasable)
                    {
                        if constexpr (is_complex_v<T>)
                            vecmat_via_gemm<T>(a0, is1_n, a1, is2_n, is2_m, a2, aDn, aDm);
                        else
                            typed_gemv<T>(a1, is2_m, is2_n, a0, is1_n, a2, os_m, aDm, aDn);
                        continue;
                    }
                unsigned char* ip2 = a1;
                unsigned char* op = a2;
                for (std::int64_t j = 0; j < aDm; ++j, ip2 += is2_m, op += os_m)
                    if constexpr (is_complex_v<T>)
                        typed_dotc<T, true>(a0, is1_n, ip2, is2_n, op, aDn);
                    else
                        typed_dot<T>(a0, is1_n, ip2, is2_n, op, aDn);
            }
        });
    }

    void gemm(dtype aType, order aOrder, transpose aTransA, transpose aTransB, std::int64_t aM, std::int64_t aN, std::int64_t aK,
        void const* aA, std::int64_t aLda, void const* aB, std::int64_t aLdb, void* aC, std::int64_t aLdc)
    {
        auto const o = static_cast<CBLAS_ORDER>(aOrder);
        auto const ta = static_cast<CBLAS_TRANSPOSE>(aTransA), tb = static_cast<CBLAS_TRANSPOSE>(aTransB);
        auto const m = static_cast<cint>(aM), n = static_cast<cint>(aN), k = static_cast<cint>(aK);
        auto const lda = static_cast<cint>(aLda), ldb = static_cast<cint>(aLdb), ldc = static_cast<cint>(aLdc);
        switch (aType)
        {
        case dtype::Float32: typed_gemm<float>(o, ta, tb, m, n, k, aA, lda, aB, ldb, aC, ldc); break;
        case dtype::Float64: typed_gemm<double>(o, ta, tb, m, n, k, aA, lda, aB, ldb, aC, ldc); break;
        case dtype::Complex64: typed_gemm<std::complex<float>>(o, ta, tb, m, n, k, aA, lda, aB, ldb, aC, ldc); break;
        case dtype::Complex128: typed_gemm<std::complex<double>>(o, ta, tb, m, n, k, aA, lda, aB, ldb, aC, ldc); break;
        default: break;
        }
    }

    void gemv(dtype aType, order aOrder, transpose aTrans, std::int64_t aM, std::int64_t aN, void const* aA, std::int64_t aLda,
        void const* aX, std::int64_t aIncX, void* aY)
    {
        auto const o = static_cast<CBLAS_ORDER>(aOrder);
        auto const t = static_cast<CBLAS_TRANSPOSE>(aTrans);
        auto const m = static_cast<cint>(aM), n = static_cast<cint>(aN), lda = static_cast<cint>(aLda), incX = static_cast<cint>(aIncX);
        switch (aType)
        {
        case dtype::Float32:
            cblas_sgemv(o, t, m, n, 1.0f, static_cast<float const*>(aA), lda, static_cast<float const*>(aX), incX, 0.0f, static_cast<float*>(aY), 1);
            break;
        case dtype::Float64:
            cblas_dgemv(o, t, m, n, 1.0, static_cast<double const*>(aA), lda, static_cast<double const*>(aX), incX, 0.0, static_cast<double*>(aY), 1);
            break;
        case dtype::Complex64:
            cblas_cgemv(o, t, m, n, one<std::complex<float>>(), aA, lda, aX, incX, zero<std::complex<float>>(), aY, 1);
            break;
        case dtype::Complex128:
            cblas_zgemv(o, t, m, n, one<std::complex<double>>(), aA, lda, aX, incX, zero<std::complex<double>>(), aY, 1);
            break;
        default:
            break;
        }
    }

    void syrk(dtype aType, order aOrder, transpose aTrans, std::int64_t aN, std::int64_t aK, void const* aA, std::int64_t aLda, void* aC, std::int64_t aLdc)
    {
        auto const o = static_cast<CBLAS_ORDER>(aOrder);
        auto const t = static_cast<CBLAS_TRANSPOSE>(aTrans);
        auto const n = static_cast<cint>(aN), k = static_cast<cint>(aK), lda = static_cast<cint>(aLda), ldc = static_cast<cint>(aLdc);
        switch (aType)
        {
        case dtype::Float32: typed_syrk<float>(o, t, n, k, aA, lda, aC, ldc); break;
        case dtype::Float64: typed_syrk<double>(o, t, n, k, aA, lda, aC, ldc); break;
        case dtype::Complex64: typed_syrk<std::complex<float>>(o, t, n, k, aA, lda, aC, ldc); break;
        case dtype::Complex128: typed_syrk<std::complex<double>>(o, t, n, k, aA, lda, aC, ldc); break;
        default: break;
        }
    }

    void axpy(dtype aType, std::int64_t aN, void const* aAlpha, void const* aX, std::int64_t aIncX, void* aY, std::int64_t aIncY)
    {
        auto const n = static_cast<cint>(aN), incX = static_cast<cint>(aIncX), incY = static_cast<cint>(aIncY);
        switch (aType)
        {
        case dtype::Float32: cblas_saxpy(n, *static_cast<float const*>(aAlpha), static_cast<float const*>(aX), incX, static_cast<float*>(aY), incY); break;
        case dtype::Float64: cblas_daxpy(n, *static_cast<double const*>(aAlpha), static_cast<double const*>(aX), incX, static_cast<double*>(aY), incY); break;
        case dtype::Complex64: cblas_caxpy(n, aAlpha, aX, incX, aY, incY); break;
        case dtype::Complex128: cblas_zaxpy(n, aAlpha, aX, incX, aY, incY); break;
        default: break;
        }
    }
}
