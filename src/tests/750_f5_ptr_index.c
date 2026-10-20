#include <stdio.h>
/* Index through local pointer (p[i] load/store/compound). */
int main(void)
{
    int a[6];
    int *p;
    int i, s;
    for (i = 0; i < 6; i++) a[i] = i + 1;
    p = a;
    s = p[2] + p[4];
    p[1] = 20;
    p[3] += 5;
    printf("%d %d %d %d\n", s, a[1], a[3], p[5]);
    return 0;
}
