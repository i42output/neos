/*
  sort.cpp

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

  Portions of this file are derived from NumPy 2.5.3 (numpy/_core/src/npysort/quicksort.hpp,
  numpy/_core/src/npysort/npysort_heapsort.h, numpy/_core/src/npysort/selection.cpp, numpy/_core/src/common/numpy_tag.h
  and numpy/_core/src/common/npy_sort.h.src), Copyright (c) 2005-2025, NumPy Developers; All rights reserved. They are
  used under NumPy's BSD 3-Clause licence (see runtime/numpy/LICENSE.NumPy). Changes: translated from C to C++ for the
  neos NumPy runtime plugin and adapted to its object model (see runtime/numpy/NOTICE).
*/

// NumPy's sorts and selections (numpy/_core/src/npysort): the algorithm NumPy uses for each type and kind, and on x86
// CPUs the SIMD sorts of x86-simd-sort for the instruction sets NumPy dispatches them on (so that the order of equal
// elements, and of the elements a partition leaves unsorted, is NumPy's)

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <utility>
#include <vector>
#include "numpy.hpp"
#include "cast.hpp"
#include "sort.hpp"
#include "xss.hpp"

// (the std::sort instantiations that x86-simd-sort's translation units also use, compiled for the baseline
// instruction set; this translation unit is linked before theirs so that the baseline definitions are the ones kept)
template void std::sort<std::int16_t*, bool (*)(std::int16_t const&, std::int16_t const&)>(std::int16_t*, std::int16_t*, bool (*)(std::int16_t const&, std::int16_t const&));
template void std::sort<std::uint16_t*, bool (*)(std::uint16_t const&, std::uint16_t const&)>(std::uint16_t*, std::uint16_t*, bool (*)(std::uint16_t const&, std::uint16_t const&));
template void std::sort<std::int32_t*, bool (*)(std::int32_t const&, std::int32_t const&)>(std::int32_t*, std::int32_t*, bool (*)(std::int32_t const&, std::int32_t const&));
template void std::sort<std::uint32_t*, bool (*)(std::uint32_t const&, std::uint32_t const&)>(std::uint32_t*, std::uint32_t*, bool (*)(std::uint32_t const&, std::uint32_t const&));
template void std::sort<float*, bool (*)(float const&, float const&)>(float*, float*, bool (*)(float const&, float const&));
template void std::sort<long*, bool (*)(long const&, long const&)>(long*, long*, bool (*)(long const&, long const&));
template void std::sort<unsigned long*, bool (*)(unsigned long const&, unsigned long const&)>(unsigned long*, unsigned long*, bool (*)(unsigned long const&, unsigned long const&));
template void std::sort<double*, bool (*)(double const&, double const&)>(double*, double*, bool (*)(double const&, double const&));

namespace neos::numpy
{
    namespace
    {
        // --- the CPU's features (as NumPy's dispatch: X86_V4, X86_V3, AVX512_ICL)

        struct cpu_features
        {
            bool x86v4 = false;
            bool x86v3 = false;
            bool icl = false;
        };

        cpu_features const& cpu()
        {
            static cpu_features const sFeatures = []()
            {
                cpu_features f;
#if (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__)) && !defined(NEOS_NUMPY_NO_XSS)
                __builtin_cpu_init();
                f.x86v3 = __builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma") && __builtin_cpu_supports("bmi") && __builtin_cpu_supports("bmi2");
                f.x86v4 = f.x86v3 && __builtin_cpu_supports("avx512f") && __builtin_cpu_supports("avx512cd") && __builtin_cpu_supports("avx512vl") &&
                    __builtin_cpu_supports("avx512bw") && __builtin_cpu_supports("avx512dq");
                f.icl = f.x86v4 && __builtin_cpu_supports("avx512vbmi") && __builtin_cpu_supports("avx512vbmi2") && __builtin_cpu_supports("avx512vnni") &&
                    __builtin_cpu_supports("avx512bitalg") && __builtin_cpu_supports("avx512vpopcntdq") && __builtin_cpu_supports("avx512ifma");
#endif
                // (NPY_DISABLE_CPU_FEATURES, as NumPy)
                if (auto const disabled = std::getenv("NPY_DISABLE_CPU_FEATURES"))
                {
                    std::string const d = disabled;
                    if (d.find("X86_V4") != std::string::npos || d.find("AVX512") != std::string::npos)
                        f.x86v4 = f.icl = false;
                    if (d.find("AVX512_ICL") != std::string::npos)
                        f.icl = false;
                    if (d.find("X86_V3") != std::string::npos || d.find("AVX2") != std::string::npos)
                        f.x86v3 = f.x86v4 = f.icl = false;
                }
                return f;
            }();
            return sFeatures;
        }

        // --- comparisons (npy::cmp / Tag::less: NaN last)

        template <typename T>
        inline bool less(T const& a, T const& b)
        {
            if constexpr (std::is_floating_point_v<T>)
                return a < b || (b != b && a == a);
            else
                return a < b;
        }

        struct half_value { std::uint16_t bits; };

        template <>
        inline bool less<half_value>(half_value const& a, half_value const& b)
        {
            half ha, hb;
            ha.bits = a.bits;
            hb.bits = b.bits;
            float const x = static_cast<float>(ha), y = static_cast<float>(hb);
            return x < y || (y != y && x == x);
        }

        template <>
        inline bool less<std::complex<float>>(std::complex<float> const& a, std::complex<float> const& b)
        {
            if (a.real() < b.real())
                return a.imag() == a.imag() || b.imag() != b.imag();
            if (a.real() > b.real())
                return b.imag() != b.imag() && a.imag() == a.imag();
            if (a.real() == b.real() || (a.real() != a.real() && b.real() != b.real()))
                return a.imag() < b.imag() || (b.imag() != b.imag() && a.imag() == a.imag());
            return b.real() != b.real();
        }

        template <>
        inline bool less<std::complex<double>>(std::complex<double> const& a, std::complex<double> const& b)
        {
            if (a.real() < b.real())
                return a.imag() == a.imag() || b.imag() != b.imag();
            if (a.real() > b.real())
                return b.imag() != b.imag() && a.imag() == a.imag();
            if (a.real() == b.real() || (a.real() != a.real() && b.real() != b.real()))
                return a.imag() < b.imag() || (b.imag() != b.imag() && a.imag() == a.imag());
            return b.real() != b.real();
        }

        int get_msb(std::int64_t aNumber)
        {
            int depth = 0;
            auto u = static_cast<std::uint64_t>(aNumber);
            while (u >>= 1)
                ++depth;
            return depth;
        }

        constexpr std::int64_t SMALL_QUICKSORT = 15;
        constexpr int PYA_QS_STACK = 128;

        // --- heapsort (npysort_heapsort.h)

        template <typename T>
        void heapsort_(T* start, std::int64_t n)
        {
            T tmp;
            T* a = start - 1;
            std::int64_t i, j, l;
            for (l = n >> 1; l > 0; --l)
            {
                tmp = a[l];
                for (i = l, j = l << 1; j <= n;)
                {
                    if (j < n && less(a[j], a[j + 1]))
                        j += 1;
                    if (less(tmp, a[j]))
                    {
                        a[i] = a[j];
                        i = j;
                        j += j;
                    }
                    else
                        break;
                }
                a[i] = tmp;
            }
            for (; n > 1;)
            {
                tmp = a[n];
                a[n] = a[1];
                n -= 1;
                for (i = 1, j = 2; j <= n;)
                {
                    if (j < n && less(a[j], a[j + 1]))
                        j++;
                    if (less(tmp, a[j]))
                    {
                        a[i] = a[j];
                        i = j;
                        j += j;
                    }
                    else
                        break;
                }
                a[i] = tmp;
            }
        }

        template <typename T>
        void aheapsort_(T const* v, std::int64_t* tosort, std::int64_t n)
        {
            std::int64_t* a = tosort - 1;
            std::int64_t i, j, l, tmp;
            for (l = n >> 1; l > 0; --l)
            {
                tmp = a[l];
                for (i = l, j = l << 1; j <= n;)
                {
                    if (j < n && less(v[a[j]], v[a[j + 1]]))
                        j += 1;
                    if (less(v[tmp], v[a[j]]))
                    {
                        a[i] = a[j];
                        i = j;
                        j += j;
                    }
                    else
                        break;
                }
                a[i] = tmp;
            }
            for (; n > 1;)
            {
                tmp = a[n];
                a[n] = a[1];
                n -= 1;
                for (i = 1, j = 2; j <= n;)
                {
                    if (j < n && less(v[a[j]], v[a[j + 1]]))
                        j++;
                    if (less(v[tmp], v[a[j]]))
                    {
                        a[i] = a[j];
                        i = j;
                        j += j;
                    }
                    else
                        break;
                }
                a[i] = tmp;
            }
        }

        // --- quicksort (quicksort.hpp: introsort)

        template <typename T>
        void quicksort_(T* start, std::int64_t num)
        {
            T vp;
            T* pl = start;
            T* pr = pl + num - 1;
            T* stack[PYA_QS_STACK];
            T** sptr = stack;
            T *pm, *pi, *pj, *pk;
            int depth[PYA_QS_STACK];
            int* psdepth = depth;
            int cdepth = get_msb(num) * 2;
            for (;;)
            {
                if (cdepth < 0)
                {
                    heapsort_(pl, pr - pl + 1);
                    goto stack_pop;
                }
                while ((pr - pl) > SMALL_QUICKSORT)
                {
                    pm = pl + ((pr - pl) >> 1);
                    if (less(*pm, *pl)) std::swap(*pm, *pl);
                    if (less(*pr, *pm)) std::swap(*pr, *pm);
                    if (less(*pm, *pl)) std::swap(*pm, *pl);
                    vp = *pm;
                    pi = pl;
                    pj = pr - 1;
                    std::swap(*pm, *pj);
                    for (;;)
                    {
                        do { ++pi; } while (less(*pi, vp));
                        do { --pj; } while (less(vp, *pj));
                        if (pi >= pj)
                            break;
                        std::swap(*pi, *pj);
                    }
                    pk = pr - 1;
                    std::swap(*pi, *pk);
                    if (pi - pl < pr - pi)
                    {
                        *sptr++ = pi + 1;
                        *sptr++ = pr;
                        pr = pi - 1;
                    }
                    else
                    {
                        *sptr++ = pl;
                        *sptr++ = pi - 1;
                        pl = pi + 1;
                    }
                    *psdepth++ = --cdepth;
                }
                for (pi = pl + 1; pi <= pr; ++pi)
                {
                    vp = *pi;
                    pj = pi;
                    pk = pi - 1;
                    while (pj > pl && less(vp, *pk))
                        *pj-- = *pk--;
                    *pj = vp;
                }
            stack_pop:
                if (sptr == stack)
                    break;
                pr = *(--sptr);
                pl = *(--sptr);
                cdepth = *(--psdepth);
            }
        }

        template <typename T>
        void aquicksort_(T const* v, std::int64_t* tosort, std::int64_t num)
        {
            T vp;
            std::int64_t* pl = tosort;
            std::int64_t* pr = tosort + num - 1;
            std::int64_t* stack[PYA_QS_STACK];
            std::int64_t** sptr = stack;
            std::int64_t *pm, *pi, *pj, *pk, vi;
            int depth[PYA_QS_STACK];
            int* psdepth = depth;
            int cdepth = get_msb(num) * 2;
            for (;;)
            {
                if (cdepth < 0)
                {
                    aheapsort_(v, pl, pr - pl + 1);
                    goto stack_pop;
                }
                while ((pr - pl) > SMALL_QUICKSORT)
                {
                    pm = pl + ((pr - pl) >> 1);
                    if (less(v[*pm], v[*pl])) std::swap(*pm, *pl);
                    if (less(v[*pr], v[*pm])) std::swap(*pr, *pm);
                    if (less(v[*pm], v[*pl])) std::swap(*pm, *pl);
                    vp = v[*pm];
                    pi = pl;
                    pj = pr - 1;
                    std::swap(*pm, *pj);
                    for (;;)
                    {
                        do { ++pi; } while (less(v[*pi], vp));
                        do { --pj; } while (less(vp, v[*pj]));
                        if (pi >= pj)
                            break;
                        std::swap(*pi, *pj);
                    }
                    pk = pr - 1;
                    std::swap(*pi, *pk);
                    if (pi - pl < pr - pi)
                    {
                        *sptr++ = pi + 1;
                        *sptr++ = pr;
                        pr = pi - 1;
                    }
                    else
                    {
                        *sptr++ = pl;
                        *sptr++ = pi - 1;
                        pl = pi + 1;
                    }
                    *psdepth++ = --cdepth;
                }
                for (pi = pl + 1; pi <= pr; ++pi)
                {
                    vi = *pi;
                    vp = v[vi];
                    pj = pi;
                    pk = pi - 1;
                    while (pj > pl && less(vp, v[*pk]))
                        *pj-- = *pk--;
                    *pj = vi;
                }
            stack_pop:
                if (sptr == stack)
                    break;
                pr = *(--sptr);
                pl = *(--sptr);
                cdepth = *(--psdepth);
            }
        }

        // --- introselect (selection.cpp)

        constexpr std::int64_t NPY_MAX_PIVOT_STACK = 50;

        void store_pivot(std::int64_t pivot, std::int64_t kth, std::int64_t* pivots, std::int64_t* npiv)
        {
            if (pivots == nullptr)
                return;
            if (pivot == kth && *npiv == NPY_MAX_PIVOT_STACK)
                pivots[*npiv - 1] = pivot;
            else if (pivot >= kth && *npiv < NPY_MAX_PIVOT_STACK)
            {
                pivots[*npiv] = pivot;
                (*npiv) += 1;
            }
        }

        // (the elements: the values themselves, or (argpartition) the indices into them)
        template <typename T, bool Arg>
        struct selection
        {
            T* v;
            std::int64_t* tosort;
            T const& value(std::int64_t i) const { if constexpr (Arg) return v[tosort[i]]; else return v[i]; }
            void swap(std::int64_t i, std::int64_t j) const { if constexpr (Arg) std::swap(tosort[i], tosort[j]); else std::swap(v[i], v[j]); }
            selection offset(std::int64_t aOffset) const { if constexpr (Arg) return { v, tosort + aOffset }; else return { v + aOffset, tosort }; }
        };

        template <typename T, bool Arg>
        int introselect_(selection<T, Arg> s, std::int64_t num, std::int64_t kth, std::int64_t* pivots, std::int64_t* npiv);

        template <typename T, bool Arg>
        void median3_swap_(selection<T, Arg> const& s, std::int64_t low, std::int64_t mid, std::int64_t high)
        {
            if (less(s.value(high), s.value(mid))) s.swap(high, mid);
            if (less(s.value(high), s.value(low))) s.swap(high, low);
            if (less(s.value(low), s.value(mid))) s.swap(low, mid);
            s.swap(mid, low + 1);
        }

        template <typename T, bool Arg>
        std::int64_t median5_(selection<T, Arg> const& s)
        {
            if (less(s.value(1), s.value(0))) s.swap(1, 0);
            if (less(s.value(4), s.value(3))) s.swap(4, 3);
            if (less(s.value(3), s.value(0))) s.swap(3, 0);
            if (less(s.value(4), s.value(1))) s.swap(4, 1);
            if (less(s.value(2), s.value(1))) s.swap(2, 1);
            if (less(s.value(3), s.value(2)))
            {
                if (less(s.value(3), s.value(1)))
                    return 1;
                return 3;
            }
            return 2;
        }

        template <typename T, bool Arg>
        void unguarded_partition_(selection<T, Arg> const& s, T const pivot, std::int64_t* ll, std::int64_t* hh)
        {
            for (;;)
            {
                do { (*ll)++; } while (less(s.value(*ll), pivot));
                do { (*hh)--; } while (less(pivot, s.value(*hh)));
                if (*hh < *ll)
                    break;
                s.swap(*ll, *hh);
            }
        }

        template <typename T, bool Arg>
        std::int64_t median_of_median5_(selection<T, Arg> const& s, std::int64_t const num, std::int64_t* pivots, std::int64_t* npiv)
        {
            std::int64_t i, subleft;
            std::int64_t right = num - 1;
            std::int64_t nmed = (right + 1) / 5;
            for (i = 0, subleft = 0; i < nmed; i++, subleft += 5)
            {
                std::int64_t m = median5_(s.offset(subleft));
                s.swap(subleft + m, i);
            }
            if (nmed > 2)
                introselect_(s, nmed, nmed / 2, pivots, npiv);
            return nmed / 2;
        }

        template <typename T, bool Arg>
        void dumb_select_(selection<T, Arg> const& s, std::int64_t num, std::int64_t kth)
        {
            for (std::int64_t i = 0; i <= kth; i++)
            {
                std::int64_t minidx = i;
                T minval = s.value(i);
                for (std::int64_t k = i + 1; k < num; k++)
                    if (less(s.value(k), minval))
                    {
                        minidx = k;
                        minval = s.value(k);
                    }
                s.swap(i, minidx);
            }
        }

        template <typename T>
        constexpr bool inexact() { return !std::is_integral_v<T>; }

        template <typename T, bool Arg>
        int introselect_(selection<T, Arg> s, std::int64_t num, std::int64_t kth, std::int64_t* pivots, std::int64_t* npiv)
        {
            std::int64_t low = 0;
            std::int64_t high = num - 1;
            int depth_limit;
            if (npiv == nullptr)
                pivots = nullptr;
            while (pivots != nullptr && *npiv > 0)
            {
                if (pivots[*npiv - 1] > kth)
                {
                    high = pivots[*npiv - 1] - 1;
                    break;
                }
                else if (pivots[*npiv - 1] == kth)
                    return 0;
                low = pivots[*npiv - 1] + 1;
                *npiv -= 1;
            }
            if (kth - low < 3)
            {
                dumb_select_(s.offset(low), high - low + 1, kth - low);
                store_pivot(kth, kth, pivots, npiv);
                return 0;
            }
            else if (inexact<T>() && kth == num - 1)
            {
                std::int64_t maxidx = low;
                T maxval = s.value(low);
                for (std::int64_t k = low + 1; k < num; k++)
                    if (!less(s.value(k), maxval))
                    {
                        maxidx = k;
                        maxval = s.value(k);
                    }
                s.swap(kth, maxidx);
                return 0;
            }
            depth_limit = get_msb(num) * 2;
            for (; low + 1 < high;)
            {
                std::int64_t ll = low + 1;
                std::int64_t hh = high;
                if (depth_limit > 0 || hh - ll < 5)
                {
                    std::int64_t const mid = low + (high - low) / 2;
                    median3_swap_(s, low, mid, high);
                }
                else
                {
                    std::int64_t mid = ll + median_of_median5_(s.offset(ll), hh - ll, nullptr, nullptr);
                    s.swap(mid, low);
                    ll--;
                    hh++;
                }
                depth_limit--;
                unguarded_partition_(s, T{ s.value(low) }, &ll, &hh);
                s.swap(low, hh);
                if (hh != kth)
                    store_pivot(hh, kth, pivots, npiv);
                if (hh >= kth)
                    high = hh - 1;
                if (hh <= kth)
                    low = ll;
            }
            if (high == low + 1)
                if (less(s.value(high), s.value(low)))
                    s.swap(high, low);
            store_pivot(kth, kth, pivots, npiv);
            return 0;
        }

        // --- the algorithm for each type and kind

        template <typename T>
        bool simd_sort(T* aData, std::int64_t aCount)
        {
            auto const n = static_cast<std::size_t>(aCount);
            if constexpr (std::is_same_v<T, std::int32_t> || std::is_same_v<T, std::uint32_t> || std::is_same_v<T, float> ||
                std::is_same_v<T, std::int64_t> || std::is_same_v<T, std::uint64_t> || std::is_same_v<T, double>)
            {
                if (cpu().x86v4) { xss::avx512::qsort(aData, n); return true; }
                if (cpu().x86v3) { xss::avx2::qsort(aData, n); return true; }
            }
            else if constexpr (std::is_same_v<T, std::int16_t> || std::is_same_v<T, std::uint16_t>)
            {
                if (cpu().icl) { xss::icl::qsort(aData, n); return true; }
            }
            else if constexpr (std::is_same_v<T, half_value>)
            {
                if (cpu().icl) { xss::icl::qsort_half(reinterpret_cast<std::uint16_t*>(aData), n); return true; }
            }
            return false;
        }

        template <typename T>
        bool simd_select(T* aData, std::int64_t aCount, std::int64_t aKth)
        {
            auto const n = static_cast<std::size_t>(aCount);
            auto const k = static_cast<std::size_t>(aKth);
            if constexpr (std::is_same_v<T, std::int32_t> || std::is_same_v<T, std::uint32_t> || std::is_same_v<T, float> ||
                std::is_same_v<T, std::int64_t> || std::is_same_v<T, std::uint64_t> || std::is_same_v<T, double>)
            {
                if (cpu().x86v4) { xss::avx512::qselect(aData, k, n); return true; }
                if (cpu().x86v3) { xss::avx2::qselect(aData, k, n); return true; }
            }
            else if constexpr (std::is_same_v<T, std::int16_t> || std::is_same_v<T, std::uint16_t>)
            {
                if (cpu().icl) { xss::icl::qselect(aData, k, n); return true; }
            }
            else if constexpr (std::is_same_v<T, half_value>)
            {
                if (cpu().icl) { xss::icl::qselect_half(reinterpret_cast<std::uint16_t*>(aData), k, n); return true; }
            }
            return false;
        }

        template <typename T>
        bool simd_argsort(T const* aData, std::int64_t* aIndices, std::int64_t aCount)
        {
            auto const n = static_cast<std::size_t>(aCount);
            auto* const indices = reinterpret_cast<std::size_t*>(aIndices);
            if constexpr (std::is_same_v<T, std::int32_t> || std::is_same_v<T, std::uint32_t> || std::is_same_v<T, float> ||
                std::is_same_v<T, std::int64_t> || std::is_same_v<T, std::uint64_t> || std::is_same_v<T, double>)
            {
                if (cpu().x86v4) { xss::avx512::argsort(aData, indices, n); return true; }
                if (cpu().x86v3) { xss::avx2::argsort(aData, indices, n); return true; }
            }
            return false;
        }

        template <typename T>
        bool simd_argselect(T const* aData, std::int64_t* aIndices, std::int64_t aCount, std::int64_t aKth)
        {
            auto const n = static_cast<std::size_t>(aCount);
            auto const k = static_cast<std::size_t>(aKth);
            auto* const indices = reinterpret_cast<std::size_t*>(aIndices);
            if constexpr (std::is_same_v<T, std::int32_t> || std::is_same_v<T, std::uint32_t> || std::is_same_v<T, float> ||
                std::is_same_v<T, std::int64_t> || std::is_same_v<T, std::uint64_t> || std::is_same_v<T, double>)
            {
                if (cpu().x86v4) { xss::avx512::argselect(aData, indices, k, n); return true; }
                if (cpu().x86v3) { xss::avx2::argselect(aData, indices, k, n); return true; }
            }
            return false;
        }

        template <typename T>
        void sort_typed(T* aData, std::int64_t aCount, sort_kind aKind)
        {
            switch (aKind)
            {
            case sort_kind::Stable:
                std::stable_sort(aData, aData + aCount, [](T const& a, T const& b) { return less(a, b); });
                break;
            default:
                if (!simd_sort(aData, aCount))
                    quicksort_(aData, aCount);
                break;
            }
        }

        template <typename T>
        void argsort_typed(T const* aData, std::int64_t* aIndices, std::int64_t aCount, sort_kind aKind)
        {
            switch (aKind)
            {
            case sort_kind::Stable:
                std::stable_sort(aIndices, aIndices + aCount, [&](std::int64_t a, std::int64_t b) { return less(aData[a], aData[b]); });
                break;
            default:
                if (!simd_argsort(aData, aIndices, aCount))
                    aquicksort_(aData, aIndices, aCount);
                break;
            }
        }

        template <typename T>
        void partition_typed(T* aData, std::int64_t aCount, std::vector<std::int64_t> const& aKth)
        {
            std::int64_t pivots[NPY_MAX_PIVOT_STACK];
            std::int64_t npiv = 0;
            for (auto kth : aKth)
            {
                if (aKth.size() == 1u && simd_select(aData, aCount, kth))
                    continue;
                introselect_(selection<T, false>{ aData, nullptr }, aCount, kth, pivots, &npiv);
            }
        }

        template <typename T>
        void argpartition_typed(T* aData, std::int64_t* aIndices, std::int64_t aCount, std::vector<std::int64_t> const& aKth)
        {
            std::int64_t pivots[NPY_MAX_PIVOT_STACK];
            std::int64_t npiv = 0;
            for (auto kth : aKth)
            {
                if (aKth.size() == 1u && simd_argselect(aData, aIndices, aCount, kth))
                    continue;
                introselect_(selection<T, true>{ aData, aIndices }, aCount, kth, pivots, &npiv);
            }
        }

        // (the C++ type with which each dtype is sorted)
        template <typename F>
        void typed(dtype aType, F&& aFunction)
        {
            switch (aType)
            {
            case dtype::Bool: aFunction(std::uint8_t{}); break;
            case dtype::Int8: aFunction(std::int8_t{}); break;
            case dtype::UInt8: aFunction(std::uint8_t{}); break;
            case dtype::Int16: aFunction(std::int16_t{}); break;
            case dtype::UInt16: aFunction(std::uint16_t{}); break;
            case dtype::Int32: aFunction(std::int32_t{}); break;
            case dtype::UInt32: aFunction(std::uint32_t{}); break;
            case dtype::Int64: aFunction(std::int64_t{}); break;
            case dtype::UInt64: aFunction(std::uint64_t{}); break;
            case dtype::Float16: aFunction(half_value{}); break;
            case dtype::Float32: aFunction(float{}); break;
            case dtype::Float64: aFunction(double{}); break;
            case dtype::Complex64: aFunction(std::complex<float>{}); break;
            case dtype::Complex128: aFunction(std::complex<double>{}); break;
            }
        }
    }

    void sort_contiguous(dtype aType, unsigned char* aData, std::int64_t aCount, sort_kind aKind)
    {
        if (aCount <= 1)
            return;
        typed(aType, [&](auto tag)
        {
            using T = decltype(tag);
            sort_typed(reinterpret_cast<T*>(aData), aCount, aKind);
        });
    }

    void argsort_contiguous(dtype aType, unsigned char const* aData, std::int64_t* aIndices, std::int64_t aCount, sort_kind aKind)
    {
        std::iota(aIndices, aIndices + aCount, std::int64_t{ 0 });
        if (aCount <= 1)
            return;
        typed(aType, [&](auto tag)
        {
            using T = decltype(tag);
            argsort_typed(reinterpret_cast<T const*>(aData), aIndices, aCount, aKind);
        });
    }

    void partition_contiguous(dtype aType, unsigned char* aData, std::int64_t aCount, std::vector<std::int64_t> const& aKth)
    {
        if (aCount <= 1)
            return;
        typed(aType, [&](auto tag)
        {
            using T = decltype(tag);
            partition_typed(reinterpret_cast<T*>(aData), aCount, aKth);
        });
    }

    void argpartition_contiguous(dtype aType, unsigned char* aData, std::int64_t* aIndices, std::int64_t aCount, std::vector<std::int64_t> const& aKth)
    {
        std::iota(aIndices, aIndices + aCount, std::int64_t{ 0 });
        if (aCount <= 1)
            return;
        typed(aType, [&](auto tag)
        {
            using T = decltype(tag);
            argpartition_typed(reinterpret_cast<T*>(aData), aIndices, aCount, aKth);
        });
    }
}
