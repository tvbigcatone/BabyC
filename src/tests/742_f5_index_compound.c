#include <stdio.h>
/* F5 residual: Index compound assign += -= *= on local int/char arrays. */
int main(void)
{
    int a[5];
    char c[4];
    int i;
    for (i = 0; i < 5; i++) a[i] = i + 1;
    for (i = 0; i < 4; i++) c[i] = (char)(i + 2);
    a[1] += 10;
    a[2] -= 1;
    a[3] *= 2;
    c[0] += 3;
    c[1] -= 1;
    printf("%d %d %d %d\n", a[1], a[2], a[3], a[4]);
    printf("%d %d\n", c[0], c[1]);
    return 0;
}
