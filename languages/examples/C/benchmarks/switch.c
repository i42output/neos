// dispatch on a dense switch (one br_table): a bytecode interpreter counting the primes below 100000
#include <stdint.h>
#include <stdio.h>

enum { PUSH, LOAD, STORE, ADD, MUL, MOD, LE, EQ, JUMP, JZ, HALT };
typedef struct { uint8_t op; int32_t arg; } instruction;

static int64_t run(instruction const* code)
{
    int64_t stack[16], locals[4] = { 0 };
    int sp = 0;
    for (int pc = 0;;)
    {
        instruction const i = code[pc++];
        switch (i.op)
        {
        case PUSH: stack[sp++] = i.arg; break;
        case LOAD: stack[sp++] = locals[i.arg]; break;
        case STORE: locals[i.arg] = stack[--sp]; break;
        case ADD: --sp; stack[sp - 1] += stack[sp]; break;
        case MUL: --sp; stack[sp - 1] *= stack[sp]; break;
        case MOD: --sp; stack[sp - 1] %= stack[sp]; break;
        case LE: --sp; stack[sp - 1] = stack[sp - 1] <= stack[sp]; break;
        case EQ: --sp; stack[sp - 1] = stack[sp - 1] == stack[sp]; break;
        case JUMP: pc = i.arg; break;
        case JZ: if (stack[--sp] == 0) pc = i.arg; break;
        case HALT: return locals[0];
        }
    }
}

int main(void)
{
    // (count = 0, n = 2; while (n <= 99999) { d = 2; while (d * d <= n && n % d != 0) d += 1; if (!(d * d <= n)) count += 1; n += 1 })
    instruction const primes[] = {
        { PUSH, 0 }, { STORE, 0 }, { PUSH, 2 }, { STORE, 1 },
        /*  4 */ { LOAD, 1 }, { PUSH, 99999 }, { LE, 0 }, { JZ, 44 },
        /*  8 */ { PUSH, 2 }, { STORE, 2 },
        /* 10 */ { LOAD, 2 }, { LOAD, 2 }, { MUL, 0 }, { LOAD, 1 }, { LE, 0 }, { JZ, 28 },
        /* 16 */ { LOAD, 1 }, { LOAD, 2 }, { MOD, 0 }, { PUSH, 0 }, { EQ, 0 }, { JZ, 23 }, { JUMP, 28 },
        /* 23 */ { LOAD, 2 }, { PUSH, 1 }, { ADD, 0 }, { STORE, 2 }, { JUMP, 10 },
        /* 28 */ { LOAD, 2 }, { LOAD, 2 }, { MUL, 0 }, { LOAD, 1 }, { LE, 0 }, { JZ, 35 }, { JUMP, 39 },
        /* 35 */ { LOAD, 0 }, { PUSH, 1 }, { ADD, 0 }, { STORE, 0 },
        /* 39 */ { LOAD, 1 }, { PUSH, 1 }, { ADD, 0 }, { STORE, 1 }, { JUMP, 4 },
        /* 44 */ { HALT, 0 } };
    printf("%lld\n", (long long)run(primes));
    return 0;
}
