// Fibonacci numbers three ways: recursion, iteration, and memoization (in a table that grows with realloc)
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static uint64_t fib_recursive(unsigned n)
{
    return n < 2 ? n : fib_recursive(n - 1) + fib_recursive(n - 2);
}

static uint64_t fib_iterative(unsigned n)
{
    uint64_t a = 0, b = 1;
    while (n-- > 0)
    {
        uint64_t const t = a + b;
        a = b;
        b = t;
    }
    return a;
}

static uint64_t* memo;
static size_t memo_size;

static uint64_t fib_memo(unsigned n)
{
    if (n >= memo_size)
    {
        size_t const size = n + 1u;
        uint64_t* const grown = realloc(memo, size * sizeof *memo);
        if (grown == NULL)
        {
            perror("realloc");
            exit(EXIT_FAILURE);
        }
        for (size_t i = memo_size; i < size; ++i)
            grown[i] = UINT64_MAX;
        memo = grown;
        memo_size = size;
    }
    if (memo[n] == UINT64_MAX)
        memo[n] = n < 2 ? n : fib_memo(n - 1) + fib_memo(n - 2);
    return memo[n];
}

int main(void)
{
    for (unsigned i = 0; i <= 20; ++i)
        printf("%2u: %" PRIu64 "%s", i, fib_recursive(i), i % 5 == 4 ? "\n" : "  ");
    printf("\nfib(90) = %" PRIu64 " (iterative), %" PRIu64 " (memoized)\n", fib_iterative(90), fib_memo(90));
    free(memo);
    return 0;
}
