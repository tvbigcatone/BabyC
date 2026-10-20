#include <stdio.h>
/* Same step-2 class on char[] (fuzz seed 11 minimised). */
int main(void)
{
    char a0[19];
    int ix, i;
    for (ix = 0; ix < 19; ix++)
        a0[ix] = (char)(ix * 2 + 1);
    for (i = 0; i < 12; i += 2)
        a0[i % 19] &= (14) & 4095;
    for (ix = 0; ix < 12; ix++)
        printf("%d ", (int)a0[ix]);
    printf("\n");
    return 0;
}
