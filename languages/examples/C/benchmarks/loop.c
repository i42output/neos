#include <stdio.h>

int main(void)
{
    long long total = 0;
    for (long long i = 0; i < 30000000; ++i)
        total += (i * i) % 7;
    printf("%lld\n", total);
    return 0;
}
