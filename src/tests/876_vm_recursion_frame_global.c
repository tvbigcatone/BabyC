#include <stdio.h>
int g;
int rec(int n, int seed)
{
    int local = seed + n;
    if (n == 0) { g += local; return local; }
    int child;
    g += n;
    child = rec(n - 1, seed + 1);
    local += child;
    g += local;
    return local;
}
int main(void)
{
    int r;
    g = 0;
    r = rec(5, 10);
    printf("%d %d\n", r, g);
    return 0;
}
