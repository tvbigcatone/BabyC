#include <stdio.h>
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
    struct P v[100];
    int i, r, t;
    t = 0;
    for (r = 0; r < 200; r++) {
        for (i = 0; i < 100; i++) { v[i].x = i; v[i].y = r; }
        for (i = 0; i < 100; i++) t += v[i].x + v[i].y;
    }
    for (i = 0; i < 20000; i++) t += f(i);
    printf("%d\n", t);
    return 0;
}
