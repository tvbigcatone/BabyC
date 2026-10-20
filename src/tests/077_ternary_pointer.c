#include <stdio.h>

void main()
{
    int x = 10;
    int *p = 0;
    int *q = &x;

    printf("%d\n", (1 > 0) ? 3 : 4);
    printf("%d\n", p ? 1 : 2);
    printf("%d\n", q ? 1 : 2);
}
