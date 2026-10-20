#include <stdio.h>
static int calc(int x)
{
    int a, b, c, d, e, f, g, h;
    a = x + 3;
    b = a + 5;
    c = b * 7;
    d = c - 4;
    e = d ^ 3;
    f = e | 8;
    g = f & 31;
    h = g + 2;
    return h;
}
int main(void)
{
    printf("%d\n", calc(2));
    return 0;
}
