// Files: text written and read back by lines, binary records with fread and fwrite (random access with fseek), a temporary
// file, and rename and remove
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    int id;
    char name[12];
    double score;
} record;

static void check(bool ok, char const* what)
{
    if (!ok)
    {
        perror(what);
        exit(EXIT_FAILURE);
    }
}

int main(void)
{
    char name[L_tmpnam + 16];
    snprintf(name, sizeof name, "neos-files-example.txt");

    // (text)
    FILE* f = fopen(name, "w");
    check(f != NULL, "fopen");
    for (int i = 1; i <= 5; ++i)
        fprintf(f, "line %d: %.*s\n", i, i * 2, "abcdefghijklmnopqrstuvwxyz");
    fputs("the last line, without a newline", f);
    check(fclose(f) == 0, "fclose");

    f = fopen(name, "r");
    check(f != NULL, "fopen");
    char line[128];
    int lines = 0;
    while (fgets(line, sizeof line, f) != NULL)
    {
        line[strcspn(line, "\n")] = '\0';
        printf("read: \"%s\"\n", line);
        ++lines;
    }
    printf("%d lines, end of file %s, size %ld bytes\n", lines, feof(f) ? "reached" : "not reached", ftell(f));
    rewind(f);
    int c, letters = 0;
    while ((c = fgetc(f)) != EOF)
        letters += c >= 'a' && c <= 'z';
    printf("%d lower case letters\n", letters);
    fclose(f);

    // (appending, then renaming and removing)
    f = fopen(name, "a");
    fputs("\nappended", f);
    fclose(f);
    char renamed[sizeof name + 8];
    snprintf(renamed, sizeof renamed, "%s.old", name);
    check(rename(name, renamed) == 0, "rename");
    printf("renamed: the old name %s, the new %s\n", fopen(name, "r") == NULL ? "is gone" : "is there", (f = fopen(renamed, "r")) != NULL ? "is there" : "is gone");
    fseek(f, -8, SEEK_END);
    char tail[16] = "";
    fread(tail, 1, 8, f);
    printf("its last 8 bytes: \"%s\"\n", tail);
    fclose(f);
    check(remove(renamed) == 0, "remove");

    // (binary records in a temporary file)
    FILE* t = tmpfile();
    check(t != NULL, "tmpfile");
    record const records[] = { { 1, "ada", 91.5 }, { 2, "lua", 87.25 }, { 3, "c", 99.0 }, { 4, "python", 78.75 } };
    size_t const count = sizeof records / sizeof records[0];
    check(fwrite(records, sizeof records[0], count, t) == count, "fwrite");
    // (the third record, the first, then the fourth: random access)
    long const order[] = { 2, 0, 3 };
    for (size_t i = 0; i < 3; ++i)
    {
        record r;
        fseek(t, order[i] * (long)sizeof(record), SEEK_SET);
        check(fread(&r, sizeof r, 1, t) == 1, "fread");
        printf("record %ld: #%d %-7s %6.2f\n", order[i], r.id, r.name, r.score);
    }
    // (an update in place, and the total read back)
    fseek(t, 1 * (long)sizeof(record) + (long)offsetof(record, score), SEEK_SET);
    double const updated = 88.0;
    fwrite(&updated, sizeof updated, 1, t);
    rewind(t);
    double total = 0.0;
    for (record r; fread(&r, sizeof r, 1, t) == 1;)
        total += r.score;
    printf("total score %.2f, file size %ld\n", total, ftell(t));
    fclose(t);
    return 0;
}
