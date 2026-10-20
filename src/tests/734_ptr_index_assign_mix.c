#include <stdio.h>
/* Full minimised seed-9 path: s1++ then assign with p[] on both sides. */
int main(void)
{
    char s0 = 14;
    int s1 = 19;
    int a0[20];
    int ix;
    for (ix = 0; ix < 20; ix++)
        a0[ix] = ix * 1 + 6;
    int *p_a0 = a0;
    p_a0[3] = a0[12];
    s1++;
    a0[16]--;
    s1 = ((p_a0[3] + (13 - p_a0[11])) - ((s0 / ((28 & 7) + 1)) + (s1 / ((p_a0[11] & 7) + 1))));
    printf("%d\n", s1);
    return 0;
}
