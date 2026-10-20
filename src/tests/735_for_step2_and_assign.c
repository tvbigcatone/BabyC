#include <stdio.h>
/* FastInc must not activate on i+=2 (DoInc is +1-only).
 * Regression: for (i=0;i<N;i+=2) a[i]&=14 was stepping by 1. */
int main(void)
{
    int a0[19];
    int ix, i;
    for (ix = 0; ix < 19; ix++)
        a0[ix] = ix * 2 + 1;
    for (i = 0; i < 12; i += 2)
        a0[i] &= 14;
    for (ix = 0; ix < 12; ix++)
        printf("%d ", a0[ix]);
    printf("\n");
    return 0;
}
