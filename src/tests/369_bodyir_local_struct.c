#include <stdio.h>
/* Anti-regression: Body IR compiles local struct; member assign/read */
struct P { int x; int y; };
int f(int a)
{
    struct P p;
    p.x = a;
    p.y = a + 1;
    return p.x + p.y;
}
int main()
{
    int i, t, r;
    t = 0;
    for (i = 0; i < 5; i++)
        t = t + f(i);
    r = f(10);
    printf("%d\n", r);
    printf("%d\n", t);
    return 0;
}
