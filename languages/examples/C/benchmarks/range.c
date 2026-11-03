#include <stdio.h>

static long long f(int l, int m)
{
    long long count = 0;
    for (int i = 0; i < l; ++i)
        for (int j = 0; j < m; ++j)
            ++count;
    return count;
}

int main(void)
{
    printf("%lld\n", f(99999 + 1, 9999 + 1));
    return 0;
}
