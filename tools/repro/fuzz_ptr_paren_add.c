/* fuzz seed-class: p[i] + (k - p[j]) wrong on IR (gcc/NO_IR=5, IR=32).
 * Found by tools/fuzz4.py seed 9 minimization 2026-10-04. */
#include <stdio.h>
int main(void) {
    int a0[20];
    int ix;
    for (ix = 0; ix < 20; ix++) a0[ix] = ix + 6;
    int *p_a0 = a0;
    printf("%d\n", p_a0[3] + (13 - p_a0[11]));
    return 0;
}
