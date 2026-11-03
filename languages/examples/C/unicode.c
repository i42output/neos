// Unicode: UTF-8 strings (the execution character set), their characters as wide characters and as char32_t (<uchar.h>), wide
// strings and wide output, classification and case mapping (<wctype.h>)
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uchar.h>
#include <wchar.h>
#include <wctype.h>

static size_t characters(char const* s)
{
    size_t count = 0;
    mbstate_t state = { 0 };
    for (size_t n; *s != '\0'; s += n, ++count)
    {
        n = mbrlen(s, MB_CUR_MAX, &state);
        if (n == (size_t)-1 || n == (size_t)-2)
            return (size_t)-1;
    }
    return count;
}

int main(void)
{
    setlocale(LC_ALL, "C.UTF-8");
    char const* const greetings[] = { "Hello", "Grüße", "Привет", "こんにちは", "你好", "مرحبا", "😀🎉" };
    for (size_t i = 0; i < sizeof greetings / sizeof greetings[0]; ++i)
        printf("%-16s %2zu bytes, %zu characters\n", greetings[i], strlen(greetings[i]), characters(greetings[i]));

    // (the code points of a string: mbrtoc32)
    char const* s = "añ€😀";
    printf("code points of \"%s\":", s);
    mbstate_t state = { 0 };
    for (char32_t c; *s != '\0';)
    {
        size_t const n = mbrtoc32(&c, s, MB_CUR_MAX, &state);
        printf(" U+%04X", (unsigned)c);
        s += n;
    }
    printf("\n");

    // (and back: c32rtomb)
    char32_t const codes[] = { U'Ω', U'≈', U'ç', U'√', U'∫', 0x1F680 };
    char buffer[32] = "";
    char* at = buffer;
    state = (mbstate_t){ 0 };
    for (size_t i = 0; i < sizeof codes / sizeof codes[0]; ++i)
        at += c32rtomb(at, codes[i], &state);
    *at = '\0';
    printf("from code points: %s\n", buffer);

    // (wide strings)
    wchar_t const* const w = L"Ünïcödé wide string";
    wchar_t upper[64];
    size_t const length = wcslen(w);
    for (size_t i = 0; i <= length; ++i)
        upper[i] = (wchar_t)towupper((wint_t)w[i]);
    printf("%ls -> %ls (%zu wide characters)\n", w, upper, length);
    size_t letters = 0, spaces = 0;
    for (wchar_t const* p = w; *p; ++p)
    {
        letters += iswalpha((wint_t)*p) != 0;
        spaces += iswspace((wint_t)*p) != 0;
    }
    printf("%zu letters, %zu spaces\n", letters, spaces);

    // (conversions of whole strings)
    char narrow[64];
    size_t const bytes = wcstombs(narrow, w, sizeof narrow);
    wchar_t wide[64];
    size_t const wides = mbstowcs(wide, "√2 ≈ 1.414", 64);
    printf("wcstombs: %zu bytes \"%s\"; mbstowcs: %zu wide characters, the first U+%04X\n", bytes, narrow, wides, (unsigned)wide[0]);

    // (UTF-16: char16_t, with a surrogate pair)
    char16_t const u16[] = u"😀!";
    printf("u\"😀!\" in UTF-16: %04X %04X %04X (%zu units)\n", u16[0], u16[1], u16[2], sizeof u16 / sizeof u16[0] - 1);
    return 0;
}
