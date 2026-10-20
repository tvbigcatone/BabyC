#include <stdio.h>
static int takes(int a, int b, double c) { return a + b + (int)(c * 10.0); }
static double takes_fp(double x) { return x + 0.5; }
static unsigned int takes_u(unsigned int x) { return x + 1U; }
int main(void) {
    char c = 12;
    signed char sc = -9;
    short s = 1234;
    unsigned short us = 65000U;
    float f = 1.25f;
    printf("%d %d %d %u\n", takes(c, sc, f), takes(s, -2, (double)f), (int)(takes_fp(f) * 10.0), takes_u(us));
    return 0;
}
