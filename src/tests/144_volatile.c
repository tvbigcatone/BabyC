#include <stdio.h>

void main()
{
    volatile int v = 10;
    int x;
    v = v + 1;
    x = v;
    printf("%d %d\n", v, x);

    volatile int a = 1;
    volatile int b = 2;
    volatile int c = a + b;
    printf("%d\n", c);
}
