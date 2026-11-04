/*
  stdatomic.h

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

/* <stdatomic.h> (7.17): atomics */

#ifndef __NEOS_STDATOMIC_H
#define __NEOS_STDATOMIC_H
#define __STDC_VERSION_STDATOMIC_H__ 202311L

#include <stddef.h>
#include <stdint.h>

/* (an _Atomic object's loads and stores (and its compound assignments, increments and decrements) are atomic (sequentially consistent:
   every order is as memory_order_seq_cst); the operations here are the compiler's __builtin_atomic_ functions; all are lock free) */
typedef enum memory_order
{
    memory_order_relaxed,
    memory_order_consume,
    memory_order_acquire,
    memory_order_release,
    memory_order_acq_rel,
    memory_order_seq_cst
} memory_order;

#define ATOMIC_BOOL_LOCK_FREE 2
#define ATOMIC_CHAR_LOCK_FREE 2
#define ATOMIC_CHAR8_T_LOCK_FREE 2
#define ATOMIC_CHAR16_T_LOCK_FREE 2
#define ATOMIC_CHAR32_T_LOCK_FREE 2
#define ATOMIC_WCHAR_T_LOCK_FREE 2
#define ATOMIC_SHORT_LOCK_FREE 2
#define ATOMIC_INT_LOCK_FREE 2
#define ATOMIC_LONG_LOCK_FREE 2
#define ATOMIC_LLONG_LOCK_FREE 2
#define ATOMIC_POINTER_LOCK_FREE 2

typedef struct atomic_flag { _Atomic bool __value; } atomic_flag;
#define ATOMIC_FLAG_INIT { false }
#define ATOMIC_VAR_INIT(value) (value)
#define kill_dependency(y) (y)

typedef _Atomic bool atomic_bool;
typedef _Atomic char atomic_char;
typedef _Atomic signed char atomic_schar;
typedef _Atomic unsigned char atomic_uchar;
typedef _Atomic short atomic_short;
typedef _Atomic unsigned short atomic_ushort;
typedef _Atomic int atomic_int;
typedef _Atomic unsigned int atomic_uint;
typedef _Atomic long atomic_long;
typedef _Atomic unsigned long atomic_ulong;
typedef _Atomic long long atomic_llong;
typedef _Atomic unsigned long long atomic_ullong;
typedef _Atomic unsigned char atomic_char8_t;
typedef _Atomic unsigned short atomic_char16_t;
typedef _Atomic unsigned int atomic_char32_t;
typedef _Atomic int atomic_wchar_t;
typedef _Atomic int_least8_t atomic_int_least8_t;
typedef _Atomic uint_least8_t atomic_uint_least8_t;
typedef _Atomic int_least16_t atomic_int_least16_t;
typedef _Atomic uint_least16_t atomic_uint_least16_t;
typedef _Atomic int_least32_t atomic_int_least32_t;
typedef _Atomic uint_least32_t atomic_uint_least32_t;
typedef _Atomic int_least64_t atomic_int_least64_t;
typedef _Atomic uint_least64_t atomic_uint_least64_t;
typedef _Atomic int_fast8_t atomic_int_fast8_t;
typedef _Atomic uint_fast8_t atomic_uint_fast8_t;
typedef _Atomic int_fast16_t atomic_int_fast16_t;
typedef _Atomic uint_fast16_t atomic_uint_fast16_t;
typedef _Atomic int_fast32_t atomic_int_fast32_t;
typedef _Atomic uint_fast32_t atomic_uint_fast32_t;
typedef _Atomic int_fast64_t atomic_int_fast64_t;
typedef _Atomic uint_fast64_t atomic_uint_fast64_t;
typedef _Atomic intptr_t atomic_intptr_t;
typedef _Atomic uintptr_t atomic_uintptr_t;
typedef _Atomic size_t atomic_size_t;
typedef _Atomic ptrdiff_t atomic_ptrdiff_t;
typedef _Atomic intmax_t atomic_intmax_t;
typedef _Atomic uintmax_t atomic_uintmax_t;

#define atomic_init(obj, value) ((void)(*(obj) = (value)))
void atomic_thread_fence(memory_order order);
void atomic_signal_fence(memory_order order);
bool atomic_flag_test_and_set(volatile atomic_flag* object);
bool atomic_flag_test_and_set_explicit(volatile atomic_flag* object, memory_order order);
void atomic_flag_clear(volatile atomic_flag* object);
void atomic_flag_clear_explicit(volatile atomic_flag* object, memory_order order);
#define atomic_thread_fence(order) ((void)(order), __builtin_atomic_fence())
#define atomic_signal_fence(order) ((void)(order))
#define atomic_is_lock_free(obj) ((void)(obj), true)
#define atomic_store(object, desired) ((void)(*(object) = (desired)))
#define atomic_store_explicit(object, desired, order) atomic_store(object, desired)
#define atomic_load(object) (*(object))
#define atomic_load_explicit(object, order) atomic_load(object)
#define atomic_exchange(object, desired) __builtin_atomic_exchange((object), (desired))
#define atomic_exchange_explicit(object, desired, order) atomic_exchange(object, desired)
#define atomic_compare_exchange_strong(object, expected, desired) __builtin_atomic_compare_exchange((object), (expected), (desired))
#define atomic_compare_exchange_strong_explicit(object, expected, desired, success, failure) atomic_compare_exchange_strong(object, expected, desired)
#define atomic_compare_exchange_weak(object, expected, desired) atomic_compare_exchange_strong(object, expected, desired)
#define atomic_compare_exchange_weak_explicit(object, expected, desired, success, failure) atomic_compare_exchange_strong(object, expected, desired)
#define atomic_fetch_add(object, operand) __builtin_atomic_fetch_add((object), (operand))
#define atomic_fetch_add_explicit(object, operand, order) atomic_fetch_add(object, operand)
#define atomic_fetch_sub(object, operand) __builtin_atomic_fetch_sub((object), (operand))
#define atomic_fetch_sub_explicit(object, operand, order) atomic_fetch_sub(object, operand)
#define atomic_fetch_or(object, operand) __builtin_atomic_fetch_or((object), (operand))
#define atomic_fetch_or_explicit(object, operand, order) atomic_fetch_or(object, operand)
#define atomic_fetch_xor(object, operand) __builtin_atomic_fetch_xor((object), (operand))
#define atomic_fetch_xor_explicit(object, operand, order) atomic_fetch_xor(object, operand)
#define atomic_fetch_and(object, operand) __builtin_atomic_fetch_and((object), (operand))
#define atomic_fetch_and_explicit(object, operand, order) atomic_fetch_and(object, operand)
#define atomic_flag_test_and_set(object) __builtin_atomic_exchange(&(object)->__value, true)
#define atomic_flag_test_and_set_explicit(object, order) atomic_flag_test_and_set(object)
#define atomic_flag_clear(object) ((void)((object)->__value = false))
#define atomic_flag_clear_explicit(object, order) atomic_flag_clear(object)

#endif
