#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    // (as the other languages' benchmark: a string made for each number, its length summed)
    long long total = 0;
    char* t = NULL;
    for (int i = 0; i < 300000; ++i)
    {
        int const n = snprintf(NULL, 0, "item %d", i);
        t = realloc(t, (size_t)n + 1u);
        snprintf(t, (size_t)n + 1u, "item %d", i);
        total += (long long)strlen(t);
    }
    free(t);
    printf("%lld\n", total);
    return 0;
}
