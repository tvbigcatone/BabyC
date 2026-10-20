#include <stdio.h>

void main()
{
    char *s = "ab\
cd";
    printf("%s\n", s);
    printf("%d\n", (int)sizeof("x"));
}
