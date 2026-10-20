#include <stdio.h>
union U { int i; char c[4]; };
struct W { union U u; int n; };
int main()
{
    union U u;
    struct W w;
    u.i = 1;
    w.u.i = 2;
    w.n = 3;
    printf("%d %d %d\n", u.c[0], w.u.c[0], w.n);
    return 0;
}
