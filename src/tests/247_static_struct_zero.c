#include <stdio.h>
struct S { int x; int y; char n[4]; };
static struct S g;
int main()
{
    static struct S l;
    printf("%d %d %d\n", g.x, g.y, g.n[0]);
    printf("%d %d %d\n", l.x, l.y, l.n[1]);
    l.x = 3;
    printf("%d\n", l.x);
    return 0;
}
