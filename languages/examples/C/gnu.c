// GNU C extensions that code written for GCC uses: attributes (constructor and destructor functions, cleanup, packed and aligned
// layouts, #pragma pack), statement expressions, typeof, case ranges and __builtin functions
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// (constructor functions: called before main, by priority; destructor functions: after main returns (or exit), in the reverse order)
__attribute__((constructor(101))) static void first(void) { printf("constructor (priority 101)\n"); }
__attribute__((constructor)) static void second(void) { printf("constructor\n"); }
__attribute__((destructor)) static void goodbye(void) { printf("destructor\n"); }

// (cleanup: a function called with a variable's address when its scope is left)
static void free_buffer(char** p)
{
    printf("  (the copy freed)\n");
    free(*p);
}
#define autofree __attribute__((cleanup(free_buffer)))

static int count_words(char const* text)
{
    autofree char* copy = strdup(text);
    int n = 0;
    for (char* w = strtok(copy, " "); w != NULL; w = strtok(NULL, " "))
        if (strcmp(w, "stop") == 0)
            return n;       // (copy freed here too)
        else
            ++n;
    return n;
}

// (layouts: packed (no padding), aligned (at least), and #pragma pack's limit)
struct __attribute__((packed)) header { unsigned char kind; unsigned int length; unsigned short flags : 9, version : 7; };
struct slot { char tag; double value __attribute__((aligned(32))); };
#pragma pack(push, 2)
struct record { char c; int i; long long l; };
#pragma pack(pop)

// (statement expressions and typeof: a maximum that evaluates its operands once)
#define max(a, b) ({ typeof(a) _a = (a); typeof(b) _b = (b); _a > _b ? _a : _b; })

static char const* grade(int score)
{
    switch (score)
    {
    case 90 ... 100: return "A";
    case 80 ... 89: return "B";
    case 70 ... 79: return "C";
    default: return "below C";
    }
}

int main(void)
{
    printf("main\n");
    printf("words: %d\n", count_words("one two three"));
    printf("words before stop: %d\n", count_words("alpha beta stop gamma"));
    struct header h = { 7, 1234, 300, 5 };
    printf("header: size %zu, length at %zu; flags %u, version %u\n", sizeof h, offsetof(struct header, length), h.flags, h.version);
    printf("slot: size %zu, alignment %zu, value at %zu\n", sizeof(struct slot), _Alignof(struct slot), offsetof(struct slot, value));
    printf("record: size %zu, i at %zu, l at %zu\n", sizeof(struct record), offsetof(struct record, i), offsetof(struct record, l));
    int i = 3;
    int const m = max(i++, 2);
    printf("max(i++, 2) = %d, then i = %d\n", m, i);
    printf("max(2.5, 1) = %g\n", max(2.5, 1.0));
    for (int score = 65; score <= 95; score += 10)
        printf("%d: %s\n", score, grade(score));
    printf("bits: popcount(255) = %d, clz(1) = %d, ctz(8) = %d\n", __builtin_popcount(255u), __builtin_clz(1u), __builtin_ctz(8u));
    return 0;
}
