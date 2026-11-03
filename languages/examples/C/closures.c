// Closures in C: a function pointer with a pointer to its environment; higher-order functions (map, filter, fold) over arrays,
// and counters whose state outlives the call that made them
#include <stdio.h>
#include <stdlib.h>

typedef struct
{
    int (*call)(void* environment, int value);
    void* environment;
} closure;

static int apply(closure f, int value)
{
    return f.call(f.environment, value);
}

static void map(int* values, size_t count, closure f)
{
    for (size_t i = 0; i < count; ++i)
        values[i] = apply(f, values[i]);
}

static size_t filter(int* values, size_t count, closure predicate)
{
    size_t kept = 0;
    for (size_t i = 0; i < count; ++i)
        if (apply(predicate, values[i]))
            values[kept++] = values[i];
    return kept;
}

static int fold(int const* values, size_t count, int initial, int (*f)(int, int))
{
    for (size_t i = 0; i < count; ++i)
        initial = f(initial, values[i]);
    return initial;
}

// (an adder: its environment the amount added)
static int add(void* environment, int value) { return value + *(int*)environment; }
static closure make_adder(int* amount) { return (closure){ add, amount }; }

// (a predicate: divisible by its environment's divisor)
static int divisible(void* environment, int value) { return value % *(int const*)environment == 0; }

// (a counter: its state on the heap)
typedef struct { int count; int step; } counter_state;
static int next(void* environment, int)
{
    counter_state* s = environment;
    s->count += s->step;
    return s->count;
}

static closure make_counter(int start, int step)
{
    counter_state* s = malloc(sizeof *s);
    *s = (counter_state){ start, step };
    return (closure){ next, s };
}

static int sum(int a, int b) { return a + b; }
static int max(int a, int b) { return a > b ? a : b; }

static void print(char const* label, int const* values, size_t count)
{
    printf("%s:", label);
    for (size_t i = 0; i < count; ++i)
        printf(" %d", values[i]);
    printf("\n");
}

int main(void)
{
    int values[10];
    for (int i = 0; i < 10; ++i)
        values[i] = i + 1;
    print("values", values, 10);
    int ten = 10;
    map(values, 10, make_adder(&ten));
    print("plus 10", values, 10);
    int three = 3;
    size_t const n = filter(values, 10, (closure){ divisible, &three });
    print("divisible by 3", values, n);
    printf("sum %d, max %d\n", fold(values, n, 0, sum), fold(values, n, 0, max));

    closure evens = make_counter(0, 2), tens = make_counter(100, 10);
    for (int i = 0; i < 3; ++i)
        printf("evens %d, tens %d\n", apply(evens, 0), apply(tens, 0));
    free(evens.environment);
    free(tens.environment);

    // (an array of function pointers)
    int (*operations[])(int, int) = { sum, max };
    char const* names[] = { "sum", "max" };
    for (size_t i = 0; i < 2; ++i)
        printf("%s(4, 9) = %d\n", names[i], operations[i](4, 9));
    return 0;
}
