#include <stdio.h>
#include <string.h>

int main(void)
{
    char name[256];
    printf("Enter your name: ");
    fflush(stdout);
    if (fgets(name, sizeof name, stdin) == NULL)
        return 1;
    name[strcspn(name, "\r\n")] = '\0';
    printf("Hello %s!\n", name);
    return 0;
}
