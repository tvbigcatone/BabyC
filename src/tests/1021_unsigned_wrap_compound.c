#include <stdio.h>
int main(void) {
    unsigned char c = 250;
    c += 10;
    c <<= 6;
    c /= 3;
    unsigned short us = 65530u;
    us += 20u;
    us >>= 3;
    int si = -7;
    si /= 2;
    si %= 3;
    unsigned int u = 0xffffffffu;
    u += 2u;
    u *= 3u;
    printf("compound=%u,%u,%d\n", (unsigned)c, (unsigned)us, si);
    printf("wrap=%u\n", u);
    return 0;
}
