#include <stdio.h>
int main(void) {
    char c = 255;
    signed char sc = -2;
    unsigned char uc = 250;
    short s = -300;
    unsigned short us = 60000;
    int a = c + sc;
    int b = uc + s;
    int d = (unsigned char)255 + (unsigned short)1;
    int r1 = (-1 < 1u);
    int r2 = ((int)-1 < (unsigned long)1);
    int r3 = ((long)-1 < (unsigned int)1);
    int r4 = ((unsigned short)65535 > (short)-1);
    long signed_long = -1L;
    unsigned int unsigned_int = 1u;
    int r5 = (signed_long < unsigned_int);
    int q = (sizeof(unsigned short) > sizeof(int)) ? 7u : (unsigned short)9;
    printf("prom=%d,%d,%d,%d,%d\n", a, b, d, (int)c, (int)uc);
    printf("cmp=%d,%d,%d,%d,%d tern=%d\n", r1, r2, r3, r4, r5, q);
    return 0;
}
