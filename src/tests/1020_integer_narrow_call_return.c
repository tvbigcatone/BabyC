#include <stdio.h>
static signed char ret_sc(long x) { return (signed char)x; }
static unsigned char ret_uc(unsigned long x) { return (unsigned char)x; }
static short add_short(short x, unsigned int y) { return (short)(x + y); }
static unsigned long take_ul(unsigned short x) { return (unsigned long)x + 0x100000001UL; }
int main(void) {
    int i = 511;
    signed char sc = (signed char)i;
    unsigned char uc = (unsigned char)i;
    short s = (short)65535u;
    unsigned short us = (unsigned short)-2;
    long lv = (long)ret_sc(255L);
    unsigned long uv = ret_uc(511UL);
    short av = add_short((short)-300, 1000u);
    unsigned long wide = take_ul(us);
    printf("narrow=%d,%u,%d,%u\n", (int)sc, (unsigned int)uc, (int)s, (unsigned int)us);
    printf("callret=%ld,%lu,%d,%lu\n", lv, uv, (int)av, wide);
    return 0;
}
