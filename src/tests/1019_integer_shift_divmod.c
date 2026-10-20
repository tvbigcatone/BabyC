#include <stdio.h>
#include <limits.h>
int main(void) {
    unsigned int top = (unsigned int)1 << (sizeof(unsigned int) * CHAR_BIT - 1);
    unsigned int wrap = top >> (sizeof(unsigned int) * CHAR_BIT - 2);
    unsigned long long top64 = (unsigned long long)1 << (sizeof(unsigned long long) * CHAR_BIT - 1);
    unsigned long long wrap64 = top64 >> (sizeof(unsigned long long) * CHAR_BIT - 2);
    int neg = -8;
    int q1 = -17 / 5;
    int r1 = -17 % 5;
    int q2 = 17 / -5;
    int r2 = 17 % -5;
    int q3 = -17 / -5;
    int r3 = -17 % -5;
    unsigned int uq = (unsigned int)-17 / 5u;
    unsigned int ur = (unsigned int)-17 % 5u;
    printf("shift=%u,%u,%d\n", top, wrap, neg >> 2);
    printf("shift64=%llu,%llu\n", top64, wrap64);
    printf("divmod=%d,%d,%d,%d,%d,%d\n", q1, r1, q2, r2, q3, r3);
    printf("unsigned=%u,%u\n", uq, ur);
    return 0;
}
