#include <stdio.h>
int main(void)
{
    signed char c = 127;
    unsigned char u = 255;
    short s = 32767;
    unsigned short us = 65535;
    int a, b, d, e;
    a = ++c;
    b = ++u;
    d = ++s;
    e = ++us;
    printf("%d %d %d %d\n", a + c, b + u, d + s, e + us);
    return 0;
}
