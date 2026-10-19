// blas.hpp — NumPy's loops that use BLAS (OpenBLAS: 3rdparty/OpenBLAS-0.3.34, built as NumPy's): NumPy's dot functions
// (arraytypes.c.src, vdot.c) and its matmul, vecdot, matvec and vecmat loops (matmul.c.src), and the BLAS calls of
// np.dot (cblasfuncs.c); the BLAS types are float32, float64, complex64 and complex128
#pragma once

#include "numpy.hpp"

namespace neos::numpy::blas
{
    bool is_blas_type(dtype aType);

    // the dot product of aN elements (TYPE_dot: cblas ?dot or ?dotu_sub if the strides (bytes) are BLAS strides)
    void dot(dtype aType, unsigned char* aIp1, std::int64_t aIs1, unsigned char* aIp2, std::int64_t aIs2, unsigned char* aOp, std::int64_t aN);
    // the dot product with the first operand conjugated: np.vdot's (TYPE_vdot) and vecdot's (TYPE_dotc)
    void vdot(dtype aType, unsigned char* aIp1, std::int64_t aIs1, unsigned char* aIp2, std::int64_t aIs2, unsigned char* aOp, std::int64_t aN);
    void dotc(dtype aType, unsigned char* aIp1, std::int64_t aIs1, unsigned char* aIp2, std::int64_t aIs2, unsigned char* aOp, std::int64_t aN);

    // the gufunc loops (aOuter: the number of outer iterations; aArgs: the operands' pointers, advanced by aOuterSteps; core
    // strides (bytes) as NumPy's)
    // matmul (m?,n),(n,p?)->(m?,p?): aSteps: is1_m, is1_n, is2_n, is2_p, os_m, os_p
    void matmul(dtype aType, std::int64_t aOuter, unsigned char** aArgs, std::int64_t const* aOuterSteps, std::int64_t aDm, std::int64_t aDn, std::int64_t aDp, std::int64_t const* aSteps);
    // vecdot (n),(n)->()
    void vecdot(dtype aType, std::int64_t aOuter, unsigned char** aArgs, std::int64_t const* aOuterSteps, std::int64_t aDn, std::int64_t aIs1, std::int64_t aIs2);
    // matvec (m,n),(n)->(m): aSteps: is1_m, is1_n, is2_n, os_m
    void matvec(dtype aType, std::int64_t aOuter, unsigned char** aArgs, std::int64_t const* aOuterSteps, std::int64_t aDm, std::int64_t aDn, std::int64_t const* aSteps);
    // vecmat (n),(n,m)->(m): aSteps: is1_n, is2_n, is2_m, os_m
    void vecmat(dtype aType, std::int64_t aOuter, unsigned char** aArgs, std::int64_t const* aOuterSteps, std::int64_t aDn, std::int64_t aDm, std::int64_t const* aSteps);

    // np.dot's BLAS calls (cblasfuncs.c: alpha one, beta zero; strides in elements)
    enum class order : int { RowMajor = 101, ColMajor = 102 };
    enum class transpose : int { NoTrans = 111, Trans = 112 };
    void gemm(dtype aType, order aOrder, transpose aTransA, transpose aTransB, std::int64_t aM, std::int64_t aN, std::int64_t aK,
        void const* aA, std::int64_t aLda, void const* aB, std::int64_t aLdb, void* aC, std::int64_t aLdc);
    void gemv(dtype aType, order aOrder, transpose aTrans, std::int64_t aM, std::int64_t aN, void const* aA, std::int64_t aLda,
        void const* aX, std::int64_t aIncX, void* aY);
    // (the upper triangle)
    void syrk(dtype aType, order aOrder, transpose aTrans, std::int64_t aN, std::int64_t aK, void const* aA, std::int64_t aLda, void* aC, std::int64_t aLdc);
    // (y += alpha * x; aAlpha points to an element)
    void axpy(dtype aType, std::int64_t aN, void const* aAlpha, void const* aX, std::int64_t aIncX, void* aY, std::int64_t aIncY);
}
