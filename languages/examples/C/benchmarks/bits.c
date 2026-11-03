// integer hashing with 32-bit unsigned operations
#include <stdint.h>
#include <stdio.h>

static uint32_t hash(uint32_t n)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < n; ++i)
    {
        h ^= i & 0xFFu;
        h *= 16777619u;
        h = (h << 5) ^ (h >> 27) ^ i;
    }
    return h;
}

int main(void)
{
    printf("%u\n", hash(20000000u));
    return 0;
}
