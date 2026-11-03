// Coroutines in C: stackless coroutines whose state is a structure and whose resumption point is a case label of a switch
// statement around the body (Duff's device: case labels within the body's nested statements); generators, and a producer and
// consumer taking turns
#include <stdio.h>

#define CO_BEGIN(state) switch ((state)->line) { case 0:
#define CO_YIELD(state, value) do { (state)->line = __LINE__; return (value); case __LINE__:; } while (0)
#define CO_END(state, value) } (state)->line = -1; return (value)
#define CO_DONE(state) ((state)->line == -1)

// --- a generator of the Fibonacci numbers below a limit

typedef struct { int line; long a, b, limit; } fibonacci;

static long fibonacci_next(fibonacci* g)
{
    CO_BEGIN(g);
    g->a = 0;
    g->b = 1;
    while (g->a < g->limit)
    {
        CO_YIELD(g, g->a);
        long const t = g->a + g->b;
        g->a = g->b;
        g->b = t;
    }
    CO_END(g, -1);
}

// --- a generator of the primes (trial division by those found so far)

typedef struct { int line; int candidate; int found[64]; int count; int i; } primes;

static int primes_next(primes* g)
{
    CO_BEGIN(g);
    for (g->candidate = 2; g->count < 64; ++g->candidate)
    {
        for (g->i = 0; g->i < g->count; ++g->i)
            if (g->candidate % g->found[g->i] == 0)
                goto composite;
        g->found[g->count++] = g->candidate;
        CO_YIELD(g, g->candidate);
    composite:;
    }
    CO_END(g, 0);
}

// --- a producer of lines (of a text), and a consumer that counts their words

typedef struct { int line; char const* text; char const* at; char current[64]; } producer;

static char const* produce(producer* p)
{
    CO_BEGIN(p);
    for (p->at = p->text; *p->at != '\0';)
    {
        size_t n = 0;
        while (*p->at != '\0' && *p->at != '\n' && n + 1u < sizeof p->current)
            p->current[n++] = *p->at++;
        p->current[n] = '\0';
        if (*p->at == '\n')
            ++p->at;
        CO_YIELD(p, p->current);
    }
    CO_END(p, NULL);
}

typedef struct { int line; int words; int lines; } consumer;

static void consume(consumer* c, char const* line)
{
    if (line == NULL)
        return;
    ++c->lines;
    bool in_word = false;
    for (char const* s = line; *s; ++s)
    {
        bool const letter = *s != ' ';
        if (letter && !in_word)
            ++c->words;
        in_word = letter;
    }
    printf("  consumer: line %d, \"%s\" (%d words so far)\n", c->lines, line, c->words);
}

int main(void)
{
    fibonacci f = { .limit = 1000 };
    printf("fibonacci:");
    for (long v = fibonacci_next(&f); !CO_DONE(&f); v = fibonacci_next(&f))
        printf(" %ld", v);
    printf("\n");

    primes p = { 0 };
    printf("primes:");
    for (int i = 0; i < 20; ++i)
        printf(" %d", primes_next(&p));
    printf("\n");

    producer pr = { .text = "the first line\nand a second\n\nthe fourth, after an empty one" };
    consumer co = { 0 };
    for (char const* line; (line = produce(&pr)) != NULL;)
        consume(&co, line);
    printf("%d lines, %d words\n", co.lines, co.words);
    return 0;
}
