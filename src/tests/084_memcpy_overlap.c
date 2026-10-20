#include <stdio.h>
#include <string.h>

char a[10];

int main()
{
    strcpy(a, "abcdef");
    memmove(&a[2], a, 4);
    printf("memmove=%s\n", a);
    strcpy(a, "abcdef");
    memcpy(&a[2], a, 4); /* must stop: overlapping memcpy is undefined in C */
    puts("ERROR: overlapping memcpy was not diagnosed");
    return 0;
}
