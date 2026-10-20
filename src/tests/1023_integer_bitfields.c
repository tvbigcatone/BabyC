#include <stdio.h>
struct Bits { signed int s:5; unsigned int u:5; signed int t:9; unsigned int v:9; };
int main(void) {
    struct Bits b;
    b.s = 14;
    b.u = 31;
    b.t = -99;
    b.v = 200;
    b.s += 1;
    b.u += 2u;
    b.t /= 3;
    b.v <<= 1;
    printf("bits=%d,%u,%d,%u\n", b.s, b.u, b.t, b.v);
    return 0;
}
