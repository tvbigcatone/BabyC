#include <stdio.h>
int main(void) {
    int i = -1;
    unsigned int u = 2u;
    long l = 5L;
    unsigned long ul = 6UL;
    unsigned long a = (i < u) ? i : u;
    long b = (l > u) ? l : u;
    unsigned long c = (ul < l) ? ul : l;
    unsigned int d = (0 ? (signed char)-3 : (unsigned short)65535);
    int e = (1 ? (unsigned char)250 : -10);
    printf("cond=%lu,%ld,%lu,%u,%d\n", a, b, c, d, e);
    printf("cmp=%d,%d,%d\n", -1 < 1u, -1L < 1u, -1 < (unsigned long)1);
    return 0;
}
