/*
  lib/stdatomic.c

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
  The C library: <stdatomic.h>'s functions (a part of the library in C: compiled with a program that uses it (see the 
  "provides" line below))
*/

/* provides: atomic_thread_fence atomic_signal_fence atomic_flag_test_and_set atomic_flag_test_and_set_explicit
    atomic_flag_clear atomic_flag_clear_explicit */

#include <stdatomic.h>

/* --- <stdatomic.h>: the functions (as well as their macros) */

void (atomic_thread_fence)(memory_order order) { atomic_thread_fence(order); }
void (atomic_signal_fence)(memory_order order) { (void)order; }
bool (atomic_flag_test_and_set)(volatile atomic_flag* object) { return atomic_flag_test_and_set(object); }
bool (atomic_flag_test_and_set_explicit)(volatile atomic_flag* object, memory_order order) { (void)order; return atomic_flag_test_and_set(object); }
void (atomic_flag_clear)(volatile atomic_flag* object) { atomic_flag_clear(object); }
void (atomic_flag_clear_explicit)(volatile atomic_flag* object, memory_order order) { (void)order; atomic_flag_clear(object); }
