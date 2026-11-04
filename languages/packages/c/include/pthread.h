/*
  pthread.h

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

/* <pthread.h> (POSIX): threads (truly concurrent: each thread an OS thread; see runtime/c/src/lib_threads.cpp). Cancellation is deferred:
   a thread that another cancels exits (with PTHREAD_CANCELED) at its next pthread_testcancel (or pthread_cond_wait, pthread_join, 
   sem_wait) */

#ifndef __NEOS_PTHREAD_H
#define __NEOS_PTHREAD_H

#include <sched.h>
#include <time.h>

typedef unsigned long long pthread_t;
typedef struct { int __detached; int __reserved; unsigned long long __stacksize; } pthread_attr_t;
/* (a mutex's, condition variable's, read-write lock's or barrier's first word: the handle of the runtime's object (0: not yet made: a 
   statically initialized one is made when it is first used)) */
typedef struct { unsigned __handle; int __type; long long __reserved[2]; } pthread_mutex_t;
typedef struct { int __type; int __pshared; } pthread_mutexattr_t;
typedef struct { unsigned __handle; int __clock; long long __reserved[2]; } pthread_cond_t;
typedef struct { int __clock; int __pshared; } pthread_condattr_t;
typedef struct { unsigned __handle; int __reserved0; long long __reserved[2]; } pthread_rwlock_t;
typedef struct { int __pshared; } pthread_rwlockattr_t;
typedef struct { unsigned __handle; int __reserved0; } pthread_barrier_t;
typedef struct { int __pshared; } pthread_barrierattr_t;
typedef unsigned pthread_key_t;
typedef int pthread_once_t;
typedef volatile int pthread_spinlock_t;

#define PTHREAD_MUTEX_INITIALIZER { 0, 0 }
#define PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP { 0, 1 }
#define PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP { 0, 2 }
#define PTHREAD_COND_INITIALIZER { 0, 0 }
#define PTHREAD_RWLOCK_INITIALIZER { 0, 0 }
#define PTHREAD_ONCE_INIT 0

#define PTHREAD_MUTEX_NORMAL 0
#define PTHREAD_MUTEX_RECURSIVE 1
#define PTHREAD_MUTEX_ERRORCHECK 2
#define PTHREAD_MUTEX_DEFAULT PTHREAD_MUTEX_NORMAL
#define PTHREAD_MUTEX_RECURSIVE_NP PTHREAD_MUTEX_RECURSIVE
#define PTHREAD_MUTEX_ERRORCHECK_NP PTHREAD_MUTEX_ERRORCHECK
#define PTHREAD_MUTEX_STALLED 0
#define PTHREAD_MUTEX_ROBUST 1
#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED 1
#define PTHREAD_PROCESS_PRIVATE 0
#define PTHREAD_PROCESS_SHARED 1
#define PTHREAD_CANCEL_ENABLE 0
#define PTHREAD_CANCEL_DISABLE 1
#define PTHREAD_CANCEL_DEFERRED 0
#define PTHREAD_CANCEL_ASYNCHRONOUS 1
#define PTHREAD_CANCELED ((void*)-1)
#define PTHREAD_BARRIER_SERIAL_THREAD (-1)
#define PTHREAD_INHERIT_SCHED 0
#define PTHREAD_EXPLICIT_SCHED 1
#define PTHREAD_SCOPE_SYSTEM 0
#define PTHREAD_SCOPE_PROCESS 1
#define PTHREAD_STACK_MIN 65536
#define PTHREAD_KEYS_MAX 1024
#define PTHREAD_DESTRUCTOR_ITERATIONS 4
#define PTHREAD_THREADS_MAX 4096

int pthread_create(pthread_t* restrict thread, const pthread_attr_t* restrict attr, void* (*start_routine)(void*), void* restrict arg);
int pthread_join(pthread_t thread, void** value_ptr);
int pthread_detach(pthread_t thread);
pthread_t pthread_self(void);
int pthread_equal(pthread_t t1, pthread_t t2);
[[noreturn]] void pthread_exit(void* value_ptr);
int pthread_cancel(pthread_t thread);
void pthread_testcancel(void);
int pthread_setcancelstate(int state, int* oldstate);
int pthread_setcanceltype(int type, int* oldtype);
int pthread_once(pthread_once_t* once_control, void (*init_routine)(void));
int pthread_yield(void);
int pthread_getconcurrency(void);
int pthread_setconcurrency(int new_level);

int pthread_attr_init(pthread_attr_t* attr);
int pthread_attr_destroy(pthread_attr_t* attr);
int pthread_attr_setdetachstate(pthread_attr_t* attr, int detachstate);
int pthread_attr_getdetachstate(const pthread_attr_t* attr, int* detachstate);
int pthread_attr_setstacksize(pthread_attr_t* attr, size_t stacksize);
int pthread_attr_getstacksize(const pthread_attr_t* restrict attr, size_t* restrict stacksize);
int pthread_attr_setscope(pthread_attr_t* attr, int contentionscope);
int pthread_attr_getscope(const pthread_attr_t* restrict attr, int* restrict contentionscope);
int pthread_attr_setinheritsched(pthread_attr_t* attr, int inheritsched);
int pthread_attr_getinheritsched(const pthread_attr_t* restrict attr, int* restrict inheritsched);
int pthread_attr_setguardsize(pthread_attr_t* attr, size_t guardsize);
int pthread_attr_getguardsize(const pthread_attr_t* restrict attr, size_t* restrict guardsize);

int pthread_mutex_init(pthread_mutex_t* restrict mutex, const pthread_mutexattr_t* restrict attr);
int pthread_mutex_destroy(pthread_mutex_t* mutex);
int pthread_mutex_lock(pthread_mutex_t* mutex);
int pthread_mutex_trylock(pthread_mutex_t* mutex);
int pthread_mutex_timedlock(pthread_mutex_t* restrict mutex, const struct timespec* restrict abstime);
int pthread_mutex_unlock(pthread_mutex_t* mutex);
int pthread_mutexattr_init(pthread_mutexattr_t* attr);
int pthread_mutexattr_destroy(pthread_mutexattr_t* attr);
int pthread_mutexattr_settype(pthread_mutexattr_t* attr, int type);
int pthread_mutexattr_gettype(const pthread_mutexattr_t* restrict attr, int* restrict type);
int pthread_mutexattr_setpshared(pthread_mutexattr_t* attr, int pshared);
int pthread_mutexattr_getpshared(const pthread_mutexattr_t* restrict attr, int* restrict pshared);

int pthread_cond_init(pthread_cond_t* restrict cond, const pthread_condattr_t* restrict attr);
int pthread_cond_destroy(pthread_cond_t* cond);
int pthread_cond_wait(pthread_cond_t* restrict cond, pthread_mutex_t* restrict mutex);
int pthread_cond_timedwait(pthread_cond_t* restrict cond, pthread_mutex_t* restrict mutex, const struct timespec* restrict abstime);
int pthread_cond_signal(pthread_cond_t* cond);
int pthread_cond_broadcast(pthread_cond_t* cond);
int pthread_condattr_init(pthread_condattr_t* attr);
int pthread_condattr_destroy(pthread_condattr_t* attr);
int pthread_condattr_setclock(pthread_condattr_t* attr, clockid_t clock_id);
int pthread_condattr_getclock(const pthread_condattr_t* restrict attr, clockid_t* restrict clock_id);
int pthread_condattr_setpshared(pthread_condattr_t* attr, int pshared);
int pthread_condattr_getpshared(const pthread_condattr_t* restrict attr, int* restrict pshared);

int pthread_rwlock_init(pthread_rwlock_t* restrict rwlock, const pthread_rwlockattr_t* restrict attr);
int pthread_rwlock_destroy(pthread_rwlock_t* rwlock);
int pthread_rwlock_rdlock(pthread_rwlock_t* rwlock);
int pthread_rwlock_tryrdlock(pthread_rwlock_t* rwlock);
int pthread_rwlock_wrlock(pthread_rwlock_t* rwlock);
int pthread_rwlock_trywrlock(pthread_rwlock_t* rwlock);
int pthread_rwlock_unlock(pthread_rwlock_t* rwlock);
int pthread_rwlockattr_init(pthread_rwlockattr_t* attr);
int pthread_rwlockattr_destroy(pthread_rwlockattr_t* attr);

int pthread_barrier_init(pthread_barrier_t* restrict barrier, const pthread_barrierattr_t* restrict attr, unsigned count);
int pthread_barrier_destroy(pthread_barrier_t* barrier);
int pthread_barrier_wait(pthread_barrier_t* barrier);
int pthread_barrierattr_init(pthread_barrierattr_t* attr);
int pthread_barrierattr_destroy(pthread_barrierattr_t* attr);

int pthread_spin_init(pthread_spinlock_t* lock, int pshared);
int pthread_spin_destroy(pthread_spinlock_t* lock);
int pthread_spin_lock(pthread_spinlock_t* lock);
int pthread_spin_trylock(pthread_spinlock_t* lock);
int pthread_spin_unlock(pthread_spinlock_t* lock);

int pthread_key_create(pthread_key_t* key, void (*destructor)(void*));
int pthread_key_delete(pthread_key_t key);
void* pthread_getspecific(pthread_key_t key);
int pthread_setspecific(pthread_key_t key, const void* value);

#endif
