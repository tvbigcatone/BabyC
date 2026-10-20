#include <stdio.h>
/* p = a (array decay) + p[i] after assign. */
int main(void)
{
    int a[5];
    int *p;
    int i;
    for (i = 0; i < 5; i++) a[i] = (i + 1) * 10;
    p = a;
    p[0] = 7;
    p[2] += 3;
    printf("%d %d %d %d\n", a[0], a[2], p[1], p[4]);
    return 0;
}
