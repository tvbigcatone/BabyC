#include <stdio.h>
int main(void)
{
    unsigned short a[4] = {0u, 32768u, 60000u, 12u};
    unsigned int u = 5u;
    int i = -1;
    unsigned int x = 0u;
    int y = 3;
    x += a[2];
    y *= (i ? u : 2u);
    ++x;
    y--;
    printf("%d %d %d %d\n", a[1] > i, a[0] < u, a[2] > 50000u, a[3] == 12);
    printf("%u %d %u\n", x, y, (unsigned)(i ? 7u : -2));
    printf("%d %d %u\n", (0, i) < a[1], (1, u) > a[0], (unsigned)(i ? 1 : 9u));
    printf("%d %d\n", ((unsigned short)65535u) > i, ((i = -2), i) < 0);
    printf("%d %d\n", (i ? -1 : 1u) < 0, (i ? -1 : 1u) == 4294967295u);
    return 0;
}
