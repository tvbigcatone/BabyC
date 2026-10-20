#include <stdio.h>
int g;
int bump(int x) { g += x; return g; }
int calc(int n)
{
    int first, child, last;
    if (n == 0) return bump(2);
    first = bump(n);
    child = calc(n - 1);
    last = bump(1);
    return first + child + last;
}
int main(void)
{
    int r;
    g = 0;
    r = calc(3);
    printf("%d %d\n", r, g);
    return 0;
}
