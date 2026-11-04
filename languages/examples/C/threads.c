// Threads: truly parallel (each an OS thread): <threads.h>'s threads, mutexes and condition variables, atomics, thread-local
// objects, and POSIX threads' barriers
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <threads.h>

#define WORKERS 4
#define LIMIT 2000000

// (the primes below LIMIT counted in parallel: each worker takes the next block of numbers (an atomic counter) and adds its count)
static atomic_int nextBlock;
static atomic_long primes;
static thread_local long found;         // (each thread's own)

static int is_prime(int n)
{
    if (n < 2)
        return 0;
    for (int d = 2; d * d <= n; ++d)
        if (n % d == 0)
            return 0;
    return 1;
}

static int count_primes(void* arg)
{
    (void)arg;
    for (int block; (block = atomic_fetch_add(&nextBlock, 1)) * 10000 < LIMIT;)
        for (int n = block * 10000; n < (block + 1) * 10000 && n < LIMIT; ++n)
            found += is_prime(n);
    primes += found;
    return (int)found;
}

// (a queue that producers and consumers share: a mutex and condition variables)
static mtx_t lock;
static cnd_t notEmpty, notFull;
static int queue[16], head, tail, closed;

static int consume(void* arg)
{
    long long* total = arg;
    for (;;)
    {
        mtx_lock(&lock);
        while (head == tail && !closed)
            cnd_wait(&notEmpty, &lock);
        if (head == tail)
        {
            mtx_unlock(&lock);
            return 0;
        }
        *total += queue[head++ % 16];
        cnd_signal(&notFull);
        mtx_unlock(&lock);
    }
}

// (phases that all the threads finish before any starts the next: a barrier)
static pthread_barrier_t phase;
static int results[WORKERS];

static void* step(void* arg)
{
    int const id = (int)(long long)arg;
    results[id] = id * id;
    pthread_barrier_wait(&phase);
    int sum = 0;
    for (int i = 0; i < WORKERS; ++i)
        sum += results[i];      // (every thread's first phase is done)
    return (void*)(long long)sum;
}

int main(void)
{
    thrd_t workers[WORKERS];
    for (int i = 0; i < WORKERS; ++i)
        thrd_create(&workers[i], count_primes, NULL);
    for (int i = 0; i < WORKERS; ++i)
    {
        int n;
        thrd_join(workers[i], &n);
    }
    printf("primes below %d: %ld (counted by %d threads)\n", LIMIT, (long)primes, WORKERS);

    mtx_init(&lock, mtx_plain);
    cnd_init(&notEmpty);
    cnd_init(&notFull);
    long long totals[2] = { 0, 0 };
    thrd_t consumers[2];
    for (int i = 0; i < 2; ++i)
        thrd_create(&consumers[i], consume, &totals[i]);
    for (int i = 1; i <= 100000; ++i)
    {
        mtx_lock(&lock);
        while (tail - head == 16)
            cnd_wait(&notFull, &lock);
        queue[tail++ % 16] = i;
        cnd_signal(&notEmpty);
        mtx_unlock(&lock);
    }
    mtx_lock(&lock);
    closed = 1;
    cnd_broadcast(&notEmpty);
    mtx_unlock(&lock);
    for (int i = 0; i < 2; ++i)
        thrd_join(consumers[i], NULL);
    printf("consumed: %lld (1 + 2 + ... + 100000 = %lld)\n", totals[0] + totals[1], 100000ll * 100001 / 2);

    pthread_t steps[WORKERS];
    pthread_barrier_init(&phase, NULL, WORKERS);
    for (int i = 0; i < WORKERS; ++i)
        pthread_create(&steps[i], NULL, step, (void*)(long long)i);
    for (int i = 0; i < WORKERS; ++i)
    {
        void* sum;
        pthread_join(steps[i], &sum);
        if (i == 0)
            printf("after the barrier, each thread sees the sum of squares: %lld\n", (long long)sum);
    }
    pthread_barrier_destroy(&phase);
    return 0;
}
