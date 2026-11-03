// Errors in C: return codes and errno, assertions of invariants, signals, and exceptions made of setjmp and longjmp (a stack
// of handlers: TRY, CATCH and THROW, as macros)
#include <errno.h>
#include <fenv.h>
#include <math.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- exceptions

typedef struct handler
{
    jmp_buf environment;
    struct handler* previous;
    char const* message;
} handler;

static handler* handlers;

#define TRY \
    { \
        handler h_ = { .previous = handlers }; \
        handlers = &h_; \
        int const code_ = setjmp(h_.environment); \
        if (code_ == 0) \
        {
#define CATCH(code, message) \
            handlers = h_.previous; \
        } \
        else \
        { \
            handlers = h_.previous; \
            int const code = code_; \
            char const* const message = h_.message;
#define END_TRY \
        } \
    }

[[noreturn]] static void throw_error(int code, char const* message)
{
    if (handlers == NULL)
    {
        fprintf(stderr, "unhandled error %d: %s\n", code, message);
        exit(EXIT_FAILURE);
    }
    handlers->message = message;
    longjmp(handlers->environment, code);
}

static int checked_divide(int a, int b)
{
    if (b == 0)
        throw_error(1, "division by zero");
    return a / b;
}

static double checked_sqrt(double x)
{
    if (x < 0.0)
        throw_error(2, "square root of a negative number");
    return sqrt(x);
}

static int depth;

static void recurse(int n)
{
    ++depth;
    if (n == 0)
        throw_error(3, "bottom of the recursion");
    recurse(n - 1);
}

// --- signals

static volatile sig_atomic_t signalled;

static void on_signal(int number)
{
    signalled = number;
}

int main(void)
{
    // (errno, set by the library)
    errno = 0;
    FILE* f = fopen("/no/such/file.txt", "r");
    if (f == NULL)
        printf("fopen failed: errno %s\n", errno == ENOENT ? "ENOENT" : "set");
    errno = 0;
    long const big = strtol("99999999999999999999", NULL, 10);
    printf("strtol: %ld, %s\n", big, errno == ERANGE ? "ERANGE" : "no error");
    // (a domain error: reported by errno, or by the floating-point exception, as math_errhandling says)
    errno = 0;
    feclearexcept(FE_ALL_EXCEPT);
    double const l = log(-1.0);
    bool const reported = ((math_errhandling & MATH_ERRNO) && errno == EDOM) || ((math_errhandling & MATH_ERREXCEPT) && fetestexcept(FE_INVALID));
    printf("log(-1): %s, domain error %s\n", isnan(l) ? "nan" : "a number", reported ? "reported" : "not reported");

    // (exceptions)
    TRY
        printf("10 / 2 = %d\n", checked_divide(10, 2));
        printf("10 / 0 = %d\n", checked_divide(10, 0));
        printf("not reached\n");
    CATCH(code, message)
        printf("caught %d: %s\n", code, message);
    END_TRY

    TRY
        TRY
            printf("sqrt(2) = %.6f\n", checked_sqrt(2.0));
            checked_sqrt(-1.0);
        CATCH(code, message)
            printf("inner handler caught %d (%s), rethrowing\n", code, message);
            throw_error(code + 100, "rethrown");
        END_TRY
    CATCH(code, message)
        printf("outer handler caught %d: %s\n", code, message);
    END_TRY

    TRY
        recurse(50);
    CATCH(code, message)
        printf("caught %d after %d calls: %s\n", code, depth, message);
    END_TRY

    // (signals)
    signal(SIGINT, on_signal);
    raise(SIGINT);
    printf("signal handler ran: %s\n", signalled == SIGINT ? "SIGINT" : "no");

    // (strerror)
    printf("strerror(EDOM): %s\n", strerror(EDOM) != NULL ? "a message" : "none");
    return 0;
}
