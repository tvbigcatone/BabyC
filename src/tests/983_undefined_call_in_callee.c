#include <stdio.h>
/* Undeclared function reached only on the second call of g, inside an
 * expression: linear must run g(0) fully, then report the tree's
 * "'foo' is undefined" at the same position. */
int g(int i)
{
    int r = i * 10;
    if (i == 1)
        r = foo(3, r) + 2;
    printf("g %d\n", r);
    return r;
}
int main(void)
{
    int s = g(0);
    s += g(1);
    printf("not reached %d\n", s);
    return 0;
}
