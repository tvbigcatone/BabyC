#include <stdio.h>
/* W37: integer loop kernels (array fill/accum) must not run on loops that
 * touch floating-point data (they wrote/read double arrays as int). */
double g[8];
int main(void)
{
    double a[4];
    double s = 0.0;
    int i, n = 3;
    for (i = 0; i < 4; i++) a[i] = i * 0.5;
    printf("fill: %f %f\n", a[1], a[3]);
    for (i = 0; i < 4; i++) s += a[i];
    printf("sum=%f\n", s);
    s = 0.0;
    for (i = 0; i < 4; i++) s += a[i] * n;
    printf("sum*n=%f\n", s);
    for (i = 0; i < 8; i++) g[i] = i + 1.0;
    s = 0.0;
    for (i = 0; i < 8; i++) s += g[i];
    printf("global sum=%f\n", s);
    for (i = 0; i < 8; i++) g[i] = 2.0;
    s = 0.0;
    for (i = 0; i < 8; i++) s += g[i];
    printf("global const fill sum=%f\n", s);
    return 0;
}
