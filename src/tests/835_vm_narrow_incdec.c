#include <stdio.h>
int f(int n) {
    short t = 32760; signed char c = 120; unsigned char u = 250; unsigned short us = 65530;
    int i, s = 0;
    for (i = 0; i < n; i++) { t++; c++; u++; us++; s += t + c + u + us; }
    return s;
}
int g(int n) {
    short t2 = -32760; signed char c2 = -120; int i, s = 0;
    for (i = 0; i < n; i++) { t2--; c2--; s += t2 + c2; }
    return s;
}
int main(void) {
    short a = 0; short v = 32767; unsigned char uc = 0; int k, r = 0;
    a--; printf("%ld\n", (long)a);
    v++; printf("%ld\n", (long)v);
    uc--; printf("%d\n", (int)uc);
    printf("%d %d\n", f(20), g(20));
    r = f(10) + g(10); k = r;
    printf("%d\n", r);
    return 0;
}
