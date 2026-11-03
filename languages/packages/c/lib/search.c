/*
  lib/search.c

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
*/

/*
  The C library: bsearch and qsort (they call the program's comparison functions) (a part of the library in C: compiled with a program that uses it (see the 
  "provides" line below))
*/

/* provides: bsearch qsort */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* --- <stdlib.h>: searching and sorting */

void* bsearch(const void* key, const void* base, size_t nmemb, size_t size, int (*compar)(const void*, const void*))
{
    const char* low = base;
    size_t n = nmemb;
    while (n > 0)
    {
        const char* middle = low + (n / 2) * size;
        int c = compar(key, middle);
        if (c == 0)
            return (void*)middle;
        if (c > 0)
        {
            low = middle + size;
            n -= n / 2 + 1;
        }
        else
            n /= 2;
    }
    return nullptr;
}

static void __neos_swap(char* a, char* b, size_t size)
{
    if (size == sizeof(long long) && ((uintptr_t)a | (uintptr_t)b) % sizeof(long long) == 0)
    {
        long long t = *(long long*)a;
        *(long long*)a = *(long long*)b;
        *(long long*)b = t;
        return;
    }
    if (size == sizeof(int) && ((uintptr_t)a | (uintptr_t)b) % sizeof(int) == 0)
    {
        int t = *(int*)a;
        *(int*)a = *(int*)b;
        *(int*)b = t;
        return;
    }
    while (size-- > 0)
    {
        char t = *a;
        *a++ = *b;
        *b++ = t;
    }
}

/* (an introsort: quicksort (median of three), heapsort when it recurses too deeply, insertion sort for short ranges) */
static void __neos_sift(char* base, size_t root, size_t n, size_t size, int (*compar)(const void*, const void*))
{
    for (;;)
    {
        size_t child = 2 * root + 1;
        if (child >= n)
            return;
        if (child + 1 < n && compar(base + child * size, base + (child + 1) * size) < 0)
            ++child;
        if (compar(base + root * size, base + child * size) >= 0)
            return;
        __neos_swap(base + root * size, base + child * size, size);
        root = child;
    }
}

static void __neos_sort(char* base, size_t n, size_t size, int (*compar)(const void*, const void*), int depth)
{
    while (n > 12)
    {
        if (depth-- == 0)
        {
            for (size_t i = n / 2; i-- > 0;)
                __neos_sift(base, i, n, size, compar);
            for (size_t i = n; i-- > 1;)
            {
                __neos_swap(base, base + i * size, size);
                __neos_sift(base, 0, i, size, compar);
            }
            return;
        }
        char* first = base;
        char* middle = base + (n / 2) * size;
        char* last = base + (n - 1) * size;
        if (compar(middle, first) < 0)
            __neos_swap(middle, first, size);
        if (compar(last, middle) < 0)
        {
            __neos_swap(last, middle, size);
            if (compar(middle, first) < 0)
                __neos_swap(middle, first, size);
        }
        /* (the pivot at the start) */
        __neos_swap(first, middle, size);
        size_t i = 1, j = n - 1;
        for (;;)
        {
            while (i <= j && compar(base + i * size, first) < 0)
                ++i;
            while (j >= i && compar(base + j * size, first) > 0)
                --j;
            if (i >= j)
                break;
            __neos_swap(base + i * size, base + j * size, size);
            ++i;
            --j;
        }
        __neos_swap(first, base + j * size, size);
        /* (the smaller side recursively, the larger iteratively) */
        size_t left = j;
        size_t right = n - j - 1;
        if (left < right)
        {
            __neos_sort(base, left, size, compar, depth);
            base += (j + 1) * size;
            n = right;
        }
        else
        {
            __neos_sort(base + (j + 1) * size, right, size, compar, depth);
            n = left;
        }
    }
    for (size_t i = 1; i < n; ++i)
        for (size_t k = i; k > 0 && compar(base + (k - 1) * size, base + k * size) > 0; --k)
            __neos_swap(base + (k - 1) * size, base + k * size, size);
}

void qsort(void* base, size_t nmemb, size_t size, int (*compar)(const void*, const void*))
{
    int depth = 0;
    for (size_t n = nmemb; n > 1; n >>= 1)
        depth += 2;
    if (nmemb > 1 && size > 0)
        __neos_sort(base, nmemb, size, compar, depth);
}
