#include <stdbool.h>
#include <stdio.h>

static bool is_prime(int n)
{
    if (n < 2)
        return false;
    for (int d = 2; d * d <= n; ++d)
        if (n % d == 0)
            return false;
    return true;
}

int main(void)
{
    int count = 0;
    for (int n = 0; n < 2000000; ++n)
        if (is_prime(n))
            ++count;
    printf("%d\n", count);
    return 0;
}
