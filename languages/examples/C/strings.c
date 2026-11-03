// Strings: <string.h>, <ctype.h>, formatting into buffers, tokenizing, and a growable string builder
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    char* data;
    size_t length;
    size_t capacity;
} builder;

[[gnu::format(printf, 2, 3)]]
static void append(builder* b, char const* format, ...)
{
    va_list args;
    va_start(args, format);
    va_list copy;
    va_copy(copy, args);
    int const n = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (b->length + (size_t)n + 1u > b->capacity)
    {
        b->capacity = (b->length + (size_t)n + 1u) * 2u;
        b->data = realloc(b->data, b->capacity);
    }
    vsnprintf(b->data + b->length, b->capacity - b->length, format, args);
    b->length += (size_t)n;
    va_end(args);
}

static void reverse(char* s)
{
    for (char* e = s + strlen(s); s < --e; ++s)
    {
        char const t = *s;
        *s = *e;
        *e = t;
    }
}

static int compare(void const* a, void const* b)
{
    return strcmp(*(char const* const*)a, *(char const* const*)b);
}

int main(void)
{
    char text[] = "the quick brown fox jumps over the lazy dog";
    printf("length %zu, 'fox' at %td\n", strlen(text), strstr(text, "fox") - text);

    char upper[sizeof text];
    for (size_t i = 0; i < sizeof text; ++i)
        upper[i] = (char)toupper((unsigned char)text[i]);
    printf("%s\n", upper);

    char* words[16];
    size_t count = 0;
    for (char* w = strtok(text, " "); w != NULL && count < 16; w = strtok(NULL, " "))
        words[count++] = w;
    qsort(words, count, sizeof words[0], compare);
    builder b = { 0 };
    for (size_t i = 0; i < count; ++i)
        append(&b, "%s%s", i == 0 ? "" : ", ", words[i]);
    printf("sorted: %s\n", b.data);

    char palindrome[] = "A man, a plan, a canal: Panama";
    char letters[sizeof palindrome];
    size_t n = 0;
    for (char const* p = palindrome; *p; ++p)
        if (isalpha((unsigned char)*p))
            letters[n++] = (char)tolower((unsigned char)*p);
    letters[n] = '\0';
    char reversed[sizeof palindrome];
    strcpy(reversed, letters);
    reverse(reversed);
    printf("'%s' is %sa palindrome\n", palindrome, strcmp(letters, reversed) == 0 ? "" : "not ");

    char number[32];
    snprintf(number, sizeof number, "%08.3f|%-6d|%+x|%e", 3.14159, 42, 255, 12345.678);
    printf("%s\n", number);
    long const parsed = strtol("  -1234xyz", &(char*){ NULL }, 10);
    printf("strtol: %ld, atof: %g\n", parsed, atof("2.5e3"));
    free(b.data);
    return 0;
}
