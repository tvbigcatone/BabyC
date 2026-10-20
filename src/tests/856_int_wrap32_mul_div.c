#include <stdio.h>
int main(void)
{
    int a = 100000, b = 100000, r1, r2;
    r1 = (a * b) / 10;      /* a*b overflows int32: wraps with -fwrapv */
    r2 = (a * b) >> 4;
    printf("%d %d\n", r1, r2);
    return 0;
}
