#include <stdio.h>

static int steps(long long n)
{
    int s = 0;
    while (n != 1)
    {
        n = n % 2 == 0 ? n / 2 : 3 * n + 1;
        ++s;
    }
    return s;
}

int main(void)
{
    int best = 0;
    long long arg = 0;
    for (long long i = 1; i < 1000000; ++i)
    {
        int const k = steps(i);
        if (k > best)
        {
            best = k;
            arg = i;
        }
    }
    printf("%lld %d\n", arg, best);
    return 0;
}
