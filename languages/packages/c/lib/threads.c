/*
  lib/threads.c

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
  The C library: <threads.h>, <pthread.h> and <semaphore.h> (a part of the library in C: compiled with a program that uses it (see the 
  "provides" line below)); their objects and OS threads are the runtime's (runtime/c/src/lib_threads.cpp)
*/

/* provides: thrd_create thrd_current thrd_detach thrd_equal thrd_exit thrd_join thrd_sleep thrd_yield
    mtx_init mtx_destroy mtx_lock mtx_timedlock mtx_trylock mtx_unlock
    cnd_init cnd_destroy cnd_wait cnd_timedwait cnd_signal cnd_broadcast tss_create tss_delete tss_get tss_set
    pthread_create pthread_join pthread_detach pthread_self pthread_equal pthread_exit pthread_cancel pthread_testcancel
    pthread_setcancelstate pthread_setcanceltype pthread_once pthread_yield pthread_getconcurrency pthread_setconcurrency
    pthread_attr_init pthread_attr_destroy pthread_attr_setdetachstate pthread_attr_getdetachstate pthread_attr_setstacksize
    pthread_attr_getstacksize pthread_attr_setscope pthread_attr_getscope pthread_attr_setinheritsched pthread_attr_getinheritsched
    pthread_attr_setguardsize pthread_attr_getguardsize
    pthread_mutex_init pthread_mutex_destroy pthread_mutex_lock pthread_mutex_trylock pthread_mutex_timedlock pthread_mutex_unlock
    pthread_mutexattr_init pthread_mutexattr_destroy pthread_mutexattr_settype pthread_mutexattr_gettype pthread_mutexattr_setpshared
    pthread_mutexattr_getpshared
    pthread_cond_init pthread_cond_destroy pthread_cond_wait pthread_cond_timedwait pthread_cond_signal pthread_cond_broadcast
    pthread_condattr_init pthread_condattr_destroy pthread_condattr_setclock pthread_condattr_getclock pthread_condattr_setpshared
    pthread_condattr_getpshared
    pthread_rwlock_init pthread_rwlock_destroy pthread_rwlock_rdlock pthread_rwlock_tryrdlock pthread_rwlock_wrlock
    pthread_rwlock_trywrlock pthread_rwlock_unlock pthread_rwlockattr_init pthread_rwlockattr_destroy
    pthread_barrier_init pthread_barrier_destroy pthread_barrier_wait pthread_barrierattr_init pthread_barrierattr_destroy
    pthread_spin_init pthread_spin_destroy pthread_spin_lock pthread_spin_trylock pthread_spin_unlock
    pthread_key_create pthread_key_delete pthread_getspecific pthread_setspecific
    sem_init sem_destroy sem_wait sem_trywait sem_timedwait sem_post sem_getvalue */

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>
#include <threads.h>

/* (the runtime's: their results are POSIX's error numbers (0: success)) */
int __neos_thread_create(void* thread, long long (*wrapper)(long long, long long), long long func, long long arg, long long stack_size, int detached);
int __neos_thread_join(unsigned long long thread, long long* result);
int __neos_thread_detach(unsigned long long thread);
unsigned long long __neos_thread_self(void);
int __neos_thread_main(void);
int __neos_thread_sleep(long long seconds, long long nanoseconds);
void __neos_thread_yield(void);
int __neos_thread_cancel(unsigned long long thread);
int __neos_thread_cancelled(void);
void __neos_thread_wait_others(void);
void __neos_thread_exit(long long value);
int __neos_mutex_init(void* mutex, int type);
int __neos_mutex_lock(void* mutex);
int __neos_mutex_trylock(void* mutex);
int __neos_mutex_timedlock(void* mutex, long long seconds, long long nanoseconds);
int __neos_mutex_unlock(void* mutex);
int __neos_object_destroy(void* object);
int __neos_cond_init(void* cond);
int __neos_cond_wait(void* cond, void* mutex);
int __neos_cond_timedwait(void* cond, void* mutex, long long seconds, long long nanoseconds);
int __neos_cond_signal(void* cond);
int __neos_cond_broadcast(void* cond);
int __neos_rwlock_init(void* rwlock);
int __neos_rwlock_lock(void* rwlock, int write, int try);
int __neos_rwlock_unlock(void* rwlock);
int __neos_barrier_init(void* barrier, int count);
int __neos_barrier_wait(void* barrier);
int __neos_sem_init(void* sem, int value);
int __neos_sem_wait(void* sem, int try);
int __neos_sem_timedwait(void* sem, long long seconds, long long nanoseconds);
int __neos_sem_post(void* sem);
int __neos_sem_getvalue(void* sem, int* value);
int __neos_key_create(unsigned* key, long long destructor);
int __neos_key_delete(unsigned key);
long long __neos_key_get(unsigned key);
int __neos_key_set(unsigned key, long long value);

/* --- threads: the runtime calls a thread's wrapper, which calls its function; a thread that exits (thrd_exit, pthread_exit, 
   cancellation) stops (the runtime's __neos_thread_exit) */

static _Thread_local int __neos_cancel_state;

static long long __neos_start_thrd(long long func, long long arg)
{
    return ((thrd_start_t)func)((void*)arg);
}

static long long __neos_start_pthread(long long func, long long arg)
{
    return (long long)((void* (*)(void*))func)((void*)arg);
}

[[noreturn]] static void __neos_exit_thread(long long value)
{
    /* (the program's thread: the program ends (normally) when the other threads have finished) */
    if (__neos_thread_main())
    {
        __neos_thread_wait_others();
        exit(EXIT_SUCCESS);
    }
    __neos_thread_exit(value);
    for (;;)
        ;
}

static int __neos_thrd_result(int error)
{
    return error == 0 ? thrd_success : error == ETIMEDOUT ? thrd_timedout : error == EBUSY ? thrd_busy : error == ENOMEM ? thrd_nomem : thrd_error;
}

/* --- <threads.h> */

int thrd_create(thrd_t* thr, thrd_start_t func, void* arg)
{
    int const error = __neos_thread_create(thr, __neos_start_thrd, (long long)func, (long long)arg, 0, 0);
    return error == 0 ? thrd_success : error == EAGAIN ? thrd_nomem : thrd_error;
}

thrd_t thrd_current(void) { return __neos_thread_self(); }
int thrd_detach(thrd_t thr) { return __neos_thrd_result(__neos_thread_detach(thr)); }
int thrd_equal(thrd_t thr0, thrd_t thr1) { return thr0 == thr1; }
void thrd_exit(int res) { __neos_exit_thread(res); }

int thrd_join(thrd_t thr, int* res)
{
    long long result = 0;
    int const error = __neos_thread_join(thr, &result);
    if (error != 0)
        return thrd_error;
    if (res != NULL)
        *res = (int)result;
    return thrd_success;
}

int thrd_sleep(const struct timespec* duration, struct timespec* remaining)
{
    if (__neos_thread_sleep(duration->tv_sec, duration->tv_nsec) != 0)
        return -2;
    if (remaining != NULL)
    {
        remaining->tv_sec = 0;
        remaining->tv_nsec = 0;
    }
    return 0;
}

void thrd_yield(void) { __neos_thread_yield(); }

int mtx_init(mtx_t* mtx, int type)
{
    return __neos_thrd_result(__neos_mutex_init(mtx, (type & mtx_recursive) != 0 ? PTHREAD_MUTEX_RECURSIVE : PTHREAD_MUTEX_NORMAL));
}

void mtx_destroy(mtx_t* mtx) { __neos_object_destroy(mtx); }
int mtx_lock(mtx_t* mtx) { return __neos_thrd_result(__neos_mutex_lock(mtx)); }
int mtx_timedlock(mtx_t* restrict mtx, const struct timespec* restrict ts) { return __neos_thrd_result(__neos_mutex_timedlock(mtx, ts->tv_sec, ts->tv_nsec)); }
int mtx_trylock(mtx_t* mtx) { return __neos_thrd_result(__neos_mutex_trylock(mtx)); }
int mtx_unlock(mtx_t* mtx) { return __neos_thrd_result(__neos_mutex_unlock(mtx)); }

int cnd_init(cnd_t* cond)
{
    cond->__clock = 0;
    return __neos_thrd_result(__neos_cond_init(cond));
}

void cnd_destroy(cnd_t* cond) { __neos_object_destroy(cond); }
int cnd_wait(cnd_t* cond, mtx_t* mtx) { return __neos_thrd_result(__neos_cond_wait(cond, mtx)); }
int cnd_timedwait(cnd_t* restrict cond, mtx_t* restrict mtx, const struct timespec* restrict ts)
{
    return __neos_thrd_result(__neos_cond_timedwait(cond, mtx, ts->tv_sec, ts->tv_nsec));
}
int cnd_signal(cnd_t* cond) { return __neos_thrd_result(__neos_cond_signal(cond)); }
int cnd_broadcast(cnd_t* cond) { return __neos_thrd_result(__neos_cond_broadcast(cond)); }

int tss_create(tss_t* key, tss_dtor_t dtor) { return __neos_thrd_result(__neos_key_create(key, (long long)dtor)); }
void tss_delete(tss_t key) { __neos_key_delete(key); }
void* tss_get(tss_t key) { return (void*)__neos_key_get(key); }
int tss_set(tss_t key, void* val) { return __neos_thrd_result(__neos_key_set(key, (long long)val)); }

/* --- <pthread.h> */

int pthread_create(pthread_t* restrict thread, const pthread_attr_t* restrict attr, void* (*start_routine)(void*), void* restrict arg)
{
    return __neos_thread_create(thread, __neos_start_pthread, (long long)start_routine, (long long)arg, attr != NULL ? (long long)attr->__stacksize : 0,
        attr != NULL ? attr->__detached : 0);
}

int pthread_join(pthread_t thread, void** value_ptr)
{
    pthread_testcancel();
    long long result = 0;
    int const error = __neos_thread_join(thread, &result);
    if (error == 0 && value_ptr != NULL)
        *value_ptr = (void*)result;
    return error;
}

int pthread_detach(pthread_t thread) { return __neos_thread_detach(thread); }
pthread_t pthread_self(void) { return __neos_thread_self(); }
int pthread_equal(pthread_t t1, pthread_t t2) { return t1 == t2; }
void pthread_exit(void* value_ptr) { __neos_exit_thread((long long)value_ptr); }
int pthread_cancel(pthread_t thread) { return __neos_thread_cancel(thread); }

void pthread_testcancel(void)
{
    if (__neos_cancel_state == PTHREAD_CANCEL_ENABLE && __neos_thread_cancelled())
        __neos_exit_thread((long long)PTHREAD_CANCELED);
}

int pthread_setcancelstate(int state, int* oldstate)
{
    if (state != PTHREAD_CANCEL_ENABLE && state != PTHREAD_CANCEL_DISABLE)
        return EINVAL;
    if (oldstate != NULL)
        *oldstate = __neos_cancel_state;
    __neos_cancel_state = state;
    return 0;
}

int pthread_setcanceltype(int type, int* oldtype)
{
    if (type != PTHREAD_CANCEL_DEFERRED && type != PTHREAD_CANCEL_ASYNCHRONOUS)
        return EINVAL;
    if (oldtype != NULL)
        *oldtype = PTHREAD_CANCEL_DEFERRED;
    return 0;
}

int pthread_once(pthread_once_t* once_control, void (*init_routine)(void))
{
    call_once(once_control, init_routine);
    return 0;
}

int pthread_yield(void)
{
    __neos_thread_yield();
    return 0;
}

static int __neos_concurrency;
int pthread_getconcurrency(void) { return __neos_concurrency; }
int pthread_setconcurrency(int new_level)
{
    if (new_level < 0)
        return EINVAL;
    __neos_concurrency = new_level;
    return 0;
}

int pthread_attr_init(pthread_attr_t* attr)
{
    attr->__detached = PTHREAD_CREATE_JOINABLE;
    attr->__reserved = 0;
    attr->__stacksize = 0;
    return 0;
}

int pthread_attr_destroy(pthread_attr_t* attr) { (void)attr; return 0; }

int pthread_attr_setdetachstate(pthread_attr_t* attr, int detachstate)
{
    if (detachstate != PTHREAD_CREATE_JOINABLE && detachstate != PTHREAD_CREATE_DETACHED)
        return EINVAL;
    attr->__detached = detachstate;
    return 0;
}

int pthread_attr_getdetachstate(const pthread_attr_t* attr, int* detachstate)
{
    *detachstate = attr->__detached;
    return 0;
}

int pthread_attr_setstacksize(pthread_attr_t* attr, size_t stacksize)
{
    if (stacksize < PTHREAD_STACK_MIN)
        return EINVAL;
    attr->__stacksize = stacksize;
    return 0;
}

int pthread_attr_getstacksize(const pthread_attr_t* restrict attr, size_t* restrict stacksize)
{
    *stacksize = attr->__stacksize != 0 ? attr->__stacksize : 8u << 20;
    return 0;
}

int pthread_attr_setscope(pthread_attr_t* attr, int contentionscope)
{
    (void)attr;
    return contentionscope == PTHREAD_SCOPE_SYSTEM ? 0 : contentionscope == PTHREAD_SCOPE_PROCESS ? ENOTSUP : EINVAL;
}

int pthread_attr_getscope(const pthread_attr_t* restrict attr, int* restrict contentionscope)
{
    (void)attr;
    *contentionscope = PTHREAD_SCOPE_SYSTEM;
    return 0;
}

int pthread_attr_setinheritsched(pthread_attr_t* attr, int inheritsched)
{
    (void)attr;
    return inheritsched == PTHREAD_INHERIT_SCHED || inheritsched == PTHREAD_EXPLICIT_SCHED ? 0 : EINVAL;
}

int pthread_attr_getinheritsched(const pthread_attr_t* restrict attr, int* restrict inheritsched)
{
    (void)attr;
    *inheritsched = PTHREAD_INHERIT_SCHED;
    return 0;
}

int pthread_attr_setguardsize(pthread_attr_t* attr, size_t guardsize) { (void)attr; (void)guardsize; return 0; }

int pthread_attr_getguardsize(const pthread_attr_t* restrict attr, size_t* restrict guardsize)
{
    (void)attr;
    *guardsize = 0;
    return 0;
}

int pthread_mutex_init(pthread_mutex_t* restrict mutex, const pthread_mutexattr_t* restrict attr)
{
    return __neos_mutex_init(mutex, attr != NULL ? attr->__type : PTHREAD_MUTEX_DEFAULT);
}

int pthread_mutex_destroy(pthread_mutex_t* mutex) { return __neos_object_destroy(mutex); }
int pthread_mutex_lock(pthread_mutex_t* mutex) { return __neos_mutex_lock(mutex); }
int pthread_mutex_trylock(pthread_mutex_t* mutex) { return __neos_mutex_trylock(mutex); }
int pthread_mutex_timedlock(pthread_mutex_t* restrict mutex, const struct timespec* restrict abstime)
{
    return __neos_mutex_timedlock(mutex, abstime->tv_sec, abstime->tv_nsec);
}
int pthread_mutex_unlock(pthread_mutex_t* mutex) { return __neos_mutex_unlock(mutex); }

int pthread_mutexattr_init(pthread_mutexattr_t* attr)
{
    attr->__type = PTHREAD_MUTEX_DEFAULT;
    attr->__pshared = PTHREAD_PROCESS_PRIVATE;
    return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t* attr) { (void)attr; return 0; }

int pthread_mutexattr_settype(pthread_mutexattr_t* attr, int type)
{
    if (type != PTHREAD_MUTEX_NORMAL && type != PTHREAD_MUTEX_RECURSIVE && type != PTHREAD_MUTEX_ERRORCHECK)
        return EINVAL;
    attr->__type = type;
    return 0;
}

int pthread_mutexattr_gettype(const pthread_mutexattr_t* restrict attr, int* restrict type)
{
    *type = attr->__type;
    return 0;
}

int pthread_mutexattr_setpshared(pthread_mutexattr_t* attr, int pshared)
{
    if (pshared != PTHREAD_PROCESS_PRIVATE && pshared != PTHREAD_PROCESS_SHARED)
        return EINVAL;
    attr->__pshared = pshared;
    return 0;
}

int pthread_mutexattr_getpshared(const pthread_mutexattr_t* restrict attr, int* restrict pshared)
{
    *pshared = attr->__pshared;
    return 0;
}

int pthread_cond_init(pthread_cond_t* restrict cond, const pthread_condattr_t* restrict attr)
{
    cond->__clock = attr != NULL ? attr->__clock : CLOCK_REALTIME;
    return __neos_cond_init(cond);
}

int pthread_cond_destroy(pthread_cond_t* cond) { return __neos_object_destroy(cond); }

int pthread_cond_wait(pthread_cond_t* restrict cond, pthread_mutex_t* restrict mutex)
{
    pthread_testcancel();
    return __neos_cond_wait(cond, mutex);
}

int pthread_cond_timedwait(pthread_cond_t* restrict cond, pthread_mutex_t* restrict mutex, const struct timespec* restrict abstime)
{
    pthread_testcancel();
    long long seconds = abstime->tv_sec;
    long long nanoseconds = abstime->tv_nsec;
    if (nanoseconds < 0 || nanoseconds >= 1000000000)
        return EINVAL;
    if (cond->__clock == CLOCK_MONOTONIC)
    {
        /* (the runtime's waits end at a time of CLOCK_REALTIME's: the time from now, from now) */
        struct timespec monotonic, realtime;
        clock_gettime(CLOCK_MONOTONIC, &monotonic);
        clock_gettime(CLOCK_REALTIME, &realtime);
        seconds += realtime.tv_sec - monotonic.tv_sec;
        nanoseconds += realtime.tv_nsec - monotonic.tv_nsec;
        if (nanoseconds < 0)
        {
            nanoseconds += 1000000000;
            --seconds;
        }
        else if (nanoseconds >= 1000000000)
        {
            nanoseconds -= 1000000000;
            ++seconds;
        }
    }
    return __neos_cond_timedwait(cond, mutex, seconds, nanoseconds);
}

int pthread_cond_signal(pthread_cond_t* cond) { return __neos_cond_signal(cond); }
int pthread_cond_broadcast(pthread_cond_t* cond) { return __neos_cond_broadcast(cond); }

int pthread_condattr_init(pthread_condattr_t* attr)
{
    attr->__clock = CLOCK_REALTIME;
    attr->__pshared = PTHREAD_PROCESS_PRIVATE;
    return 0;
}

int pthread_condattr_destroy(pthread_condattr_t* attr) { (void)attr; return 0; }

int pthread_condattr_setclock(pthread_condattr_t* attr, clockid_t clock_id)
{
    if (clock_id != CLOCK_REALTIME && clock_id != CLOCK_MONOTONIC)
        return EINVAL;
    attr->__clock = clock_id;
    return 0;
}

int pthread_condattr_getclock(const pthread_condattr_t* restrict attr, clockid_t* restrict clock_id)
{
    *clock_id = attr->__clock;
    return 0;
}

int pthread_condattr_setpshared(pthread_condattr_t* attr, int pshared)
{
    if (pshared != PTHREAD_PROCESS_PRIVATE && pshared != PTHREAD_PROCESS_SHARED)
        return EINVAL;
    attr->__pshared = pshared;
    return 0;
}

int pthread_condattr_getpshared(const pthread_condattr_t* restrict attr, int* restrict pshared)
{
    *pshared = attr->__pshared;
    return 0;
}

int pthread_rwlock_init(pthread_rwlock_t* restrict rwlock, const pthread_rwlockattr_t* restrict attr)
{
    (void)attr;
    return __neos_rwlock_init(rwlock);
}

int pthread_rwlock_destroy(pthread_rwlock_t* rwlock) { return __neos_object_destroy(rwlock); }
int pthread_rwlock_rdlock(pthread_rwlock_t* rwlock) { return __neos_rwlock_lock(rwlock, 0, 0); }
int pthread_rwlock_tryrdlock(pthread_rwlock_t* rwlock) { return __neos_rwlock_lock(rwlock, 0, 1); }
int pthread_rwlock_wrlock(pthread_rwlock_t* rwlock) { return __neos_rwlock_lock(rwlock, 1, 0); }
int pthread_rwlock_trywrlock(pthread_rwlock_t* rwlock) { return __neos_rwlock_lock(rwlock, 1, 1); }
int pthread_rwlock_unlock(pthread_rwlock_t* rwlock) { return __neos_rwlock_unlock(rwlock); }
int pthread_rwlockattr_init(pthread_rwlockattr_t* attr) { attr->__pshared = PTHREAD_PROCESS_PRIVATE; return 0; }
int pthread_rwlockattr_destroy(pthread_rwlockattr_t* attr) { (void)attr; return 0; }

int pthread_barrier_init(pthread_barrier_t* restrict barrier, const pthread_barrierattr_t* restrict attr, unsigned count)
{
    (void)attr;
    if (count == 0u || count > 0x7fffffffu)
        return EINVAL;
    return __neos_barrier_init(barrier, (int)count);
}

int pthread_barrier_destroy(pthread_barrier_t* barrier) { return __neos_object_destroy(barrier); }

int pthread_barrier_wait(pthread_barrier_t* barrier)
{
    int const result = __neos_barrier_wait(barrier);
    return result == 1 ? PTHREAD_BARRIER_SERIAL_THREAD : result < 0 ? -result : 0;
}

int pthread_barrierattr_init(pthread_barrierattr_t* attr) { attr->__pshared = PTHREAD_PROCESS_PRIVATE; return 0; }
int pthread_barrierattr_destroy(pthread_barrierattr_t* attr) { (void)attr; return 0; }

/* (spin locks: an int (0: unlocked, 1: locked), exchanged atomically; a thread that waits yields) */
int pthread_spin_init(pthread_spinlock_t* lock, int pshared)
{
    (void)pshared;
    *(_Atomic int*)(int*)lock = 0;
    return 0;
}

int pthread_spin_destroy(pthread_spinlock_t* lock) { (void)lock; return 0; }

int pthread_spin_lock(pthread_spinlock_t* lock)
{
    _Atomic int* const word = (_Atomic int*)(int*)lock;
    while (__builtin_atomic_exchange(word, 1) != 0)
        for (int spins = 0; *word != 0; ++spins)
            if (spins >= 64)
                __neos_thread_yield();
    return 0;
}

int pthread_spin_trylock(pthread_spinlock_t* lock) { return __builtin_atomic_exchange((_Atomic int*)(int*)lock, 1) == 0 ? 0 : EBUSY; }

int pthread_spin_unlock(pthread_spinlock_t* lock)
{
    *(_Atomic int*)(int*)lock = 0;
    return 0;
}

int pthread_key_create(pthread_key_t* key, void (*destructor)(void*)) { return __neos_key_create(key, (long long)destructor); }
int pthread_key_delete(pthread_key_t key) { return __neos_key_delete(key); }
void* pthread_getspecific(pthread_key_t key) { return (void*)__neos_key_get(key); }
int pthread_setspecific(pthread_key_t key, const void* value) { return __neos_key_set(key, (long long)value); }

/* --- <semaphore.h> (errors: -1, errno set) */

static int __neos_sem_result(int error)
{
    if (error == 0)
        return 0;
    errno = error;
    return -1;
}

int sem_init(sem_t* sem, int pshared, unsigned value)
{
    (void)pshared;
    if (value > SEM_VALUE_MAX)
        return __neos_sem_result(EINVAL);
    return __neos_sem_result(__neos_sem_init(sem, (int)value));
}

int sem_destroy(sem_t* sem) { return __neos_sem_result(__neos_object_destroy(sem)); }

int sem_wait(sem_t* sem)
{
    pthread_testcancel();
    return __neos_sem_result(__neos_sem_wait(sem, 0));
}

int sem_trywait(sem_t* sem) { return __neos_sem_result(__neos_sem_wait(sem, 1)); }
int sem_timedwait(sem_t* restrict sem, const struct timespec* restrict abstime)
{
    if (abstime->tv_nsec < 0 || abstime->tv_nsec >= 1000000000)
        return __neos_sem_result(EINVAL);
    return __neos_sem_result(__neos_sem_timedwait(sem, abstime->tv_sec, abstime->tv_nsec));
}
int sem_post(sem_t* sem) { return __neos_sem_result(__neos_sem_post(sem)); }
int sem_getvalue(sem_t* restrict sem, int* restrict sval) { return __neos_sem_result(__neos_sem_getvalue(sem, sval)); }
