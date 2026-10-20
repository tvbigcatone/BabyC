#include <stdio.h>
enum Small { S0 = 0, S1 = 7 };
int main(void) {
    unsigned char u = 200; signed char s = -10; enum Small e = S1;
    printf("%d %d %d %d\n", (u >> 3), (s >> 1), (u < 250), (s < 0));
    printf("%d %d %d %d\n", (u & 15), (s | 3), (e << 2), (e == 7));
    return 0;
}
