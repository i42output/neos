// A JSON parser and printer: values as a tagged union (a tree), a recursive descent parser with error positions (its errors
// leave by longjmp), \u escapes as UTF-8 (surrogate pairs too), and output pretty-printed and compact
#include <ctype.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { JSON_NULL, JSON_FALSE, JSON_TRUE, JSON_NUMBER, JSON_STRING, JSON_ARRAY, JSON_OBJECT } json_kind;

typedef struct json json;

struct json
{
    json_kind kind;
    union
    {
        double number;
        char* string;
        struct { json** items; size_t count; } array;
        struct { char** keys; json** values; size_t count; } object;
    };
};

// --- parsing

typedef struct
{
    char const* text;
    char const* at;
    jmp_buf failed;
    char error[96];
} parser;

[[noreturn]] static void fail(parser* p, char const* message)
{
    snprintf(p->error, sizeof p->error, "%s at offset %td", message, p->at - p->text);
    longjmp(p->failed, 1);
}

static void* allocate(size_t size)
{
    void* const result = calloc(1, size);
    if (result == NULL)
    {
        perror("calloc");
        exit(EXIT_FAILURE);
    }
    return result;
}

static void skip_space(parser* p)
{
    while (*p->at == ' ' || *p->at == '\t' || *p->at == '\n' || *p->at == '\r')
        ++p->at;
}

static json* parse_value(parser* p);

static unsigned hex4(parser* p)
{
    unsigned v = 0;
    for (int i = 0; i < 4; ++i)
    {
        char const c = *p->at++;
        if (!isxdigit((unsigned char)c))
            fail(p, "bad \\u escape");
        v = v * 16u + (unsigned)(isdigit((unsigned char)c) ? c - '0' : tolower((unsigned char)c) - 'a' + 10);
    }
    return v;
}

static char* parse_string(parser* p)
{
    ++p->at;    // (")
    size_t capacity = 16, length = 0;
    char* s = allocate(capacity);
    for (;;)
    {
        if (length + 5u > capacity)
            s = realloc(s, capacity *= 2u);
        char c = *p->at++;
        if (c == '"')
            break;
        if (c == '\0' || (unsigned char)c < 0x20u)
            fail(p, "unterminated string");
        if (c != '\\')
        {
            s[length++] = c;
            continue;
        }
        switch (c = *p->at++)
        {
        case '"': case '\\': case '/': s[length++] = c; break;
        case 'b': s[length++] = '\b'; break;
        case 'f': s[length++] = '\f'; break;
        case 'n': s[length++] = '\n'; break;
        case 'r': s[length++] = '\r'; break;
        case 't': s[length++] = '\t'; break;
        case 'u':
        {
            unsigned long code = hex4(p);
            if (code >= 0xD800u && code < 0xDC00u)
            {
                if (p->at[0] != '\\' || p->at[1] != 'u')
                    fail(p, "unpaired surrogate");
                p->at += 2;
                unsigned const low = hex4(p);
                if (low < 0xDC00u || low >= 0xE000u)
                    fail(p, "bad surrogate pair");
                code = 0x10000u + ((code - 0xD800u) << 10) + (low - 0xDC00u);
            }
            // (as UTF-8)
            if (code < 0x80u)
                s[length++] = (char)code;
            else if (code < 0x800u)
            {
                s[length++] = (char)(0xC0u | (code >> 6));
                s[length++] = (char)(0x80u | (code & 0x3Fu));
            }
            else if (code < 0x10000u)
            {
                s[length++] = (char)(0xE0u | (code >> 12));
                s[length++] = (char)(0x80u | ((code >> 6) & 0x3Fu));
                s[length++] = (char)(0x80u | (code & 0x3Fu));
            }
            else
            {
                s[length++] = (char)(0xF0u | (code >> 18));
                s[length++] = (char)(0x80u | ((code >> 12) & 0x3Fu));
                s[length++] = (char)(0x80u | ((code >> 6) & 0x3Fu));
                s[length++] = (char)(0x80u | (code & 0x3Fu));
            }
            break;
        }
        default:
            --p->at;
            fail(p, "bad escape");
        }
    }
    s[length] = '\0';
    return s;
}

static void expect(parser* p, char c)
{
    skip_space(p);
    if (*p->at != c)
    {
        char message[32];
        snprintf(message, sizeof message, "expected '%c'", c);
        fail(p, message);
    }
    ++p->at;
}

static json* parse_value(parser* p)
{
    skip_space(p);
    json* v = allocate(sizeof *v);
    char const c = *p->at;
    if (c == '{')
    {
        v->kind = JSON_OBJECT;
        ++p->at;
        skip_space(p);
        if (*p->at == '}')
        {
            ++p->at;
            return v;
        }
        for (;;)
        {
            skip_space(p);
            if (*p->at != '"')
                fail(p, "expected a key");
            v->object.keys = realloc(v->object.keys, (v->object.count + 1u) * sizeof(char*));
            v->object.values = realloc(v->object.values, (v->object.count + 1u) * sizeof(json*));
            v->object.keys[v->object.count] = parse_string(p);
            expect(p, ':');
            v->object.values[v->object.count++] = parse_value(p);
            skip_space(p);
            if (*p->at == ',')
            {
                ++p->at;
                continue;
            }
            expect(p, '}');
            return v;
        }
    }
    if (c == '[')
    {
        v->kind = JSON_ARRAY;
        ++p->at;
        skip_space(p);
        if (*p->at == ']')
        {
            ++p->at;
            return v;
        }
        for (;;)
        {
            v->array.items = realloc(v->array.items, (v->array.count + 1u) * sizeof(json*));
            v->array.items[v->array.count++] = parse_value(p);
            skip_space(p);
            if (*p->at == ',')
            {
                ++p->at;
                continue;
            }
            expect(p, ']');
            return v;
        }
    }
    if (c == '"')
    {
        v->kind = JSON_STRING;
        v->string = parse_string(p);
        return v;
    }
    static struct { char const* word; json_kind kind; } const words[] = { { "null", JSON_NULL }, { "true", JSON_TRUE }, { "false", JSON_FALSE } };
    for (size_t i = 0; i < sizeof words / sizeof words[0]; ++i)
        if (strncmp(p->at, words[i].word, strlen(words[i].word)) == 0)
        {
            p->at += strlen(words[i].word);
            v->kind = words[i].kind;
            return v;
        }
    if (c == '-' || isdigit((unsigned char)c))
    {
        char* end;
        v->kind = JSON_NUMBER;
        v->number = strtod(p->at, &end);
        p->at = end;
        return v;
    }
    fail(p, c == '\0' ? "unexpected end" : "unexpected character");
}

static json* parse(char const* text, char* error, size_t size)
{
    parser p = { .text = text, .at = text };
    if (setjmp(p.failed) != 0)
    {
        // (the values made so far leak: a sketch, not a library)
        snprintf(error, size, "%s", p.error);
        return NULL;
    }
    json* const v = parse_value(&p);
    skip_space(&p);
    if (*p.at != '\0')
        fail(&p, "trailing characters");
    return v;
}

static void release(json* v)
{
    switch (v->kind)
    {
    case JSON_STRING:
        free(v->string);
        break;
    case JSON_ARRAY:
        for (size_t i = 0; i < v->array.count; ++i)
            release(v->array.items[i]);
        free(v->array.items);
        break;
    case JSON_OBJECT:
        for (size_t i = 0; i < v->object.count; ++i)
        {
            free(v->object.keys[i]);
            release(v->object.values[i]);
        }
        free(v->object.keys);
        free(v->object.values);
        break;
    default:
        break;
    }
    free(v);
}

// --- printing

static void print_string(char const* s)
{
    putchar('"');
    for (; *s; ++s)
        switch (*s)
        {
        case '"': fputs("\\\"", stdout); break;
        case '\\': fputs("\\\\", stdout); break;
        case '\n': fputs("\\n", stdout); break;
        case '\t': fputs("\\t", stdout); break;
        default:
            if ((unsigned char)*s < 0x20u)
                printf("\\u%04x", (unsigned)*s);
            else
                putchar(*s);
        }
    putchar('"');
}

static void print(json const* v, int indent, int depth)
{
    switch (v->kind)
    {
    case JSON_NULL: fputs("null", stdout); break;
    case JSON_FALSE: fputs("false", stdout); break;
    case JSON_TRUE: fputs("true", stdout); break;
    case JSON_NUMBER:
        if (v->number == floor(v->number) && fabs(v->number) < 1e15)
            printf("%.0f", v->number);
        else
            printf("%.17g", v->number);
        break;
    case JSON_STRING: print_string(v->string); break;
    case JSON_ARRAY:
    case JSON_OBJECT:
    {
        bool const object = v->kind == JSON_OBJECT;
        size_t const count = object ? v->object.count : v->array.count;
        putchar(object ? '{' : '[');
        for (size_t i = 0; i < count; ++i)
        {
            if (i > 0)
                putchar(',');
            if (indent > 0)
                printf("\n%*s", indent * (depth + 1), "");
            if (object)
            {
                print_string(v->object.keys[i]);
                fputs(indent > 0 ? ": " : ":", stdout);
            }
            print(object ? v->object.values[i] : v->array.items[i], indent, depth + 1);
        }
        if (indent > 0 && count > 0)
            printf("\n%*s", indent * depth, "");
        putchar(object ? '}' : ']');
        break;
    }
    }
}

static json const* member(json const* v, char const* key)
{
    if (v == NULL || v->kind != JSON_OBJECT)
        return NULL;
    for (size_t i = 0; i < v->object.count; ++i)
        if (strcmp(v->object.keys[i], key) == 0)
            return v->object.values[i];
    return NULL;
}

int main(void)
{
    char const* const text =
        "{ \"name\": \"neos\", \"version\": [1, 0, 0], \"languages\": [\"Lua\", \"Ada\", \"JavaScript\", \"Python\", \"C\"],\n"
        "  \"jit\": true, \"license\": null, \"pi\": 3.14159265358979, \"big\": -1.5e300,\n"
        "  \"escapes\": \"tab\\there \\\"quoted\\\" \\u00e9\\u4e2d\\ud83d\\ude00\", \"nested\": { \"empty\": {}, \"list\": [] } }";
    char error[128];
    json* const v = parse(text, error, sizeof error);
    if (v == NULL)
    {
        printf("error: %s\n", error);
        return EXIT_FAILURE;
    }
    print(v, 2, 0);
    printf("\n");
    print(v, 0, 0);
    printf("\n");
    json const* languages = member(v, "languages");
    printf("%zu languages, the last %s\n", languages->array.count, languages->array.items[languages->array.count - 1u]->string);
    release(v);

    char const* const bad[] = { "[1, 2,", "{\"a\" 1}", "[\"\\x\"]", "tru", "[1] 2" };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i)
        if (parse(bad[i], error, sizeof error) == NULL)
            printf("%-10s -> %s\n", bad[i], error);
    return 0;
}
