#include <stdio.h>
struct S { unsigned char uc; unsigned short us; unsigned u; };
int main(void)
{
    struct S s;
    s.uc = 200;
    s.us = 50000;
    s.u = 0xffffffffu;
    s.uc += 100;
    s.us >>= 1;
    s.u = s.u / 2;
    printf("%u %u %u\n", (unsigned)s.uc, (unsigned)s.us, s.u);
    return 0;
}
