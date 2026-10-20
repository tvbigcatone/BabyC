#include <stdio.h>
struct Bits { signed int s:5; unsigned int u:5; };
int main(void) {
    struct Bits b; b.s = -7; b.u = 25;
    printf("%d %d %d %d\n", b.s < 0, b.u > 16U, b.s + 10, b.u + 10);
    printf("%d %d %d\n", (b.s < b.u), ((unsigned int)b.s < b.u), (b.u >> 2));
    return 0;
}
