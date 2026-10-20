#include <stdio.h>
/* Regression: p[i] + (k - p[j]) must not scale as pointer arithmetic.
 * Was IR=32, gcc/NO_IR=5 (fuzz4 seed class 2026-10-04). */
int main(void)
{
    int a0[20];
    int ix;
    for (ix = 0; ix < 20; ix++)
        a0[ix] = ix + 6;
    int *p_a0 = a0;
    printf("%d\n", p_a0[3] + (13 - p_a0[11]));
    printf("%d\n", p_a0[3] + (13 + p_a0[11]));
    printf("%d\n", (p_a0[3] + 13) - p_a0[11]);
    return 0;
}
