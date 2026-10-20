#include <stdio.h>
static float bump(float x) { return x + 0.5f; }
static double widen(double x) { return x * 2.0; }
int main(void)
{
    float x = 1.0f;
    float *p = &x;
    *p = 2.25f;
    printf("x=%.2f bump=%.2f widen=%.2f\n", x, bump(x), widen(x));
    *p = 3.75f;
    printf("x=%.2f bump=%.2f\n", x, bump(x));
    return 0;
}
