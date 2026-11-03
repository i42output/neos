// A tour of C23: nullptr, constexpr, typeof, auto, bool keywords, attributes, binary literals and digit separators, enums
// with fixed underlying types, _BitInt, checked arithmetic (<stdckdint.h>), bit utilities (<stdbit.h>), #embed, __VA_OPT__,
// #elifdef, empty initializers, unnamed parameters, labels before declarations, and _Generic
#include <stdbit.h>
#include <stdckdint.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LOG(format, ...) printf("log: " format "\n" __VA_OPT__(,) __VA_ARGS__)

#ifdef __STDC_NO_THREADS__
#define THREADS "no <threads.h>"
#elifdef __STDC_VERSION__
#define THREADS "<threads.h>"
#endif

#define TYPE_NAME(x) _Generic((x), bool: "bool", char: "char", int: "int", long: "long", unsigned: "unsigned", double: "double", \
    char*: "char*", nullptr_t: "nullptr_t", default: "something else")

enum colour : unsigned char { red, green, blue, colours };
static_assert(sizeof(enum colour) == 1);
static_assert(colours == 3, "three colours");

constexpr int table_size = 4;
constexpr double scale = 0.5;
static int table[table_size] = { 1, 2, 3, 4 };

[[nodiscard]] static int twice(int x) { return 2 * x; }
[[deprecated("use twice")]] static int double_it(int x) { return 2 * x; }

static int ignored(int, [[maybe_unused]] int y)     // (unnamed parameters)
{
    return 7;
}

static char const* classify(int n)
{
    switch (n)
    {
    case 0:
        return "zero";
    case 1:
        [[fallthrough]];
    case 2:
        return "one or two";
    default:
        return "many";
    }
}

int main(void)
{
    printf("__STDC_VERSION__ %ld, %s\n", __STDC_VERSION__, THREADS);

    int* p = nullptr;
    printf("nullptr: %s, %s\n", p == nullptr ? "null" : "not null", TYPE_NAME(nullptr));

    auto n = 40 + 2;
    typeof(n) m = n * 2;
    typeof_unqual(const long) l = 5;
    l += 1;
    printf("auto %s %d, typeof %d, typeof_unqual %ld\n", TYPE_NAME(n), n, m, l);

    bool const flag = true;
    printf("bool: %d %s, sizeof(true) %zu\n", flag, TYPE_NAME(flag), sizeof(true));

    unsigned const mask = 0b1011'0001;
    long long const million = 1'000'000;
    printf("binary 0b1011'0001 = %u, 1'000'000 = %lld\n", mask, million);
    printf("u8 character: %d, constexpr: %d, %g\n", u8'A', table[table_size - 1], scale * table_size);

    enum colour c = blue;
    printf("enum colour: %d of %d, %zu byte\n", c, colours, sizeof c);

    _BitInt(12) small = 2047;
    unsigned _BitInt(4) nibble = 15;
    ++nibble;
    printf("_BitInt(12) %d, unsigned _BitInt(4) 15 + 1 = %u, BITINT_MAXWIDTH %d\n", (int)small, (unsigned)nibble, BITINT_MAXWIDTH);

    int sum;
    bool const overflowed = ckd_add(&sum, INT32_MAX, 1);
    uint8_t byte;
    bool const wrapped = ckd_mul(&byte, 20, 13);
    printf("ckd_add overflow %d (%d), ckd_mul overflow %d (%u)\n", overflowed, sum, wrapped, byte);

    unsigned const x = 0x00F0;
    printf("stdc_count_ones %u, stdc_leading_zeros %u, stdc_trailing_zeros %u, stdc_bit_width %u, stdc_bit_ceil(100) %u\n",
        stdc_count_ones(x), stdc_leading_zeros(x), stdc_trailing_zeros(x), stdc_bit_width(x), stdc_bit_ceil(100u));

    static unsigned char const embedded[] = {
#embed "c23.c" limit(16)
    };
    printf("#embed: %zu bytes, \"%.13s\"\n", sizeof embedded, (char const*)embedded + 3);

    struct { int a; double b; char s[8]; } empty = {};
    printf("empty initializer: %d %g %zu\n", empty.a, empty.b, strlen(empty.s));

    printf("%d %d %s %s %s\n", twice(21), ignored(1, 2), classify(0), classify(1), classify(9));

    LOG("no arguments");
    LOG("%d arguments", 1);

    goto after;
after:
    int declared_after_label = 3;   // (a label before a declaration)
    printf("declared after a label: %d\n", declared_after_label);
    {
        LOG("a label at the end of a compound statement");
    end:
    }
    alignas(16) char aligned[16];
    printf("alignof(max_align_t) %zu, aligned %s\n", alignof(max_align_t), (uintptr_t)aligned % 16 == 0 ? "yes" : "no");
    return 0;
}
