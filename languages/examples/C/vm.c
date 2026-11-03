// A bytecode interpreter: a stack machine whose instructions are dispatched by a switch on the opcode. The opcodes are dense
// (0 to OP_COUNT - 1), so the switch compiles to one br_table: the opcode indexes a table of the cases' blocks. Programs: the
// factorials, the Fibonacci numbers, and a count of the primes below 10000 (an inner and an outer loop)
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef enum
{
    OP_PUSH,    // push the immediate
    OP_LOAD,    // push local[immediate]
    OP_STORE,   // local[immediate] = pop
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_MOD,
    OP_LT,      // push (second < top)
    OP_LE,
    OP_EQ,
    OP_JUMP,    // pc = immediate
    OP_JZ,      // pc = immediate if pop is 0
    OP_PRINT,   // print pop
    OP_HALT,
    OP_COUNT
} opcode;

static char const* const names[OP_COUNT] = { "push", "load", "store", "add", "sub", "mul", "mod", "lt", "le", "eq", "jump", "jz", "print", "halt" };

typedef struct { uint8_t op; int32_t arg; } instruction;

static long long steps;

static void run(instruction const* code, int64_t* locals)
{
    int64_t stack[64];
    int sp = 0;
    for (int pc = 0;;)
    {
        instruction const i = code[pc++];
        ++steps;
        switch ((opcode)i.op)
        {
        case OP_PUSH: stack[sp++] = i.arg; break;
        case OP_LOAD: stack[sp++] = locals[i.arg]; break;
        case OP_STORE: locals[i.arg] = stack[--sp]; break;
        case OP_ADD: --sp; stack[sp - 1] += stack[sp]; break;
        case OP_SUB: --sp; stack[sp - 1] -= stack[sp]; break;
        case OP_MUL: --sp; stack[sp - 1] *= stack[sp]; break;
        case OP_MOD: --sp; stack[sp - 1] %= stack[sp]; break;
        case OP_LT: --sp; stack[sp - 1] = stack[sp - 1] < stack[sp]; break;
        case OP_LE: --sp; stack[sp - 1] = stack[sp - 1] <= stack[sp]; break;
        case OP_EQ: --sp; stack[sp - 1] = stack[sp - 1] == stack[sp]; break;
        case OP_JUMP: pc = i.arg; break;
        case OP_JZ: if (stack[--sp] == 0) pc = i.arg; break;
        case OP_PRINT: printf("%lld ", (long long)stack[--sp]); break;
        case OP_HALT: printf("\n"); return;
        default:
            fprintf(stderr, "bad opcode %d at %d\n", i.op, pc - 1);
            exit(EXIT_FAILURE);
        }
    }
}

static void disassemble(char const* title, instruction const* code)
{
    printf("%s:", title);
    for (int pc = 0;; ++pc)
    {
        printf(" %s", names[code[pc].op]);
        if (code[pc].op <= OP_STORE || code[pc].op == OP_JUMP || code[pc].op == OP_JZ)
            printf(" %d", code[pc].arg);
        if (code[pc].op == OP_HALT)
            break;
        printf(";");
    }
    printf("\n");
}

int main(void)
{
    // (n = 1, f = 1; while (n <= 12) { f *= n; print f; n += 1 })
    instruction const factorials[] = {
        { OP_PUSH, 1 }, { OP_STORE, 0 }, { OP_PUSH, 1 }, { OP_STORE, 1 },
        { OP_LOAD, 0 }, { OP_PUSH, 12 }, { OP_LE, 0 }, { OP_JZ, 19 },
        { OP_LOAD, 1 }, { OP_LOAD, 0 }, { OP_MUL, 0 }, { OP_STORE, 1 },
        { OP_LOAD, 1 }, { OP_PRINT, 0 },
        { OP_LOAD, 0 }, { OP_PUSH, 1 }, { OP_ADD, 0 }, { OP_STORE, 0 }, { OP_JUMP, 4 },
        { OP_HALT, 0 } };
    // (a = 0, b = 1, i = 0; while (i < 20) { print a; t = a + b; a = b; b = t; i += 1 })
    instruction const fibonacci[] = {
        { OP_PUSH, 0 }, { OP_STORE, 0 }, { OP_PUSH, 1 }, { OP_STORE, 1 }, { OP_PUSH, 0 }, { OP_STORE, 2 },
        { OP_LOAD, 2 }, { OP_PUSH, 20 }, { OP_LT, 0 }, { OP_JZ, 25 },
        { OP_LOAD, 0 }, { OP_PRINT, 0 },
        { OP_LOAD, 0 }, { OP_LOAD, 1 }, { OP_ADD, 0 }, { OP_STORE, 3 },
        { OP_LOAD, 1 }, { OP_STORE, 0 }, { OP_LOAD, 3 }, { OP_STORE, 1 },
        { OP_LOAD, 2 }, { OP_PUSH, 1 }, { OP_ADD, 0 }, { OP_STORE, 2 }, { OP_JUMP, 6 },
        { OP_HALT, 0 } };
    // (count = 0, n = 2; while (n < 10000) { d = 2; while (d * d <= n && n % d != 0) d += 1; if (d * d > n) count += 1; n += 1 }
    //  print count)
    instruction const primes[] = {
        /*  0 */ { OP_PUSH, 0 }, { OP_STORE, 0 }, { OP_PUSH, 2 }, { OP_STORE, 1 },
        /*  4 */ { OP_LOAD, 1 }, { OP_PUSH, 10000 }, { OP_LT, 0 }, { OP_JZ, 44 },
        /*  8 */ { OP_PUSH, 2 }, { OP_STORE, 2 },
        /* 10 */ { OP_LOAD, 2 }, { OP_LOAD, 2 }, { OP_MUL, 0 }, { OP_LOAD, 1 }, { OP_LE, 0 }, { OP_JZ, 28 },
        /* 16 */ { OP_LOAD, 1 }, { OP_LOAD, 2 }, { OP_MOD, 0 }, { OP_PUSH, 0 }, { OP_EQ, 0 }, { OP_JZ, 23 }, { OP_JUMP, 28 },
        /* 23 */ { OP_LOAD, 2 }, { OP_PUSH, 1 }, { OP_ADD, 0 }, { OP_STORE, 2 }, { OP_JUMP, 10 },
        /* 28 */ { OP_LOAD, 2 }, { OP_LOAD, 2 }, { OP_MUL, 0 }, { OP_LOAD, 1 }, { OP_LE, 0 }, { OP_JZ, 35 }, { OP_JUMP, 39 },
        /* 35 */ { OP_LOAD, 0 }, { OP_PUSH, 1 }, { OP_ADD, 0 }, { OP_STORE, 0 },
        /* 39 */ { OP_LOAD, 1 }, { OP_PUSH, 1 }, { OP_ADD, 0 }, { OP_STORE, 1 }, { OP_JUMP, 4 },
        /* 44 */ { OP_LOAD, 0 }, { OP_PRINT, 0 }, { OP_HALT, 0 } };
    int64_t locals[8] = { 0 };
    disassemble("factorials", factorials);
    run(factorials, locals);
    run(fibonacci, locals);
    run(primes, locals);
    printf("%lld instructions executed\n", steps);
    return 0;
}
