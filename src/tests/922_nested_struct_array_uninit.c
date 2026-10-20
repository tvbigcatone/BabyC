#include <stdio.h>
struct P { int x; int y; };
struct L { struct P p[2]; int n; };
int main(void) {
    struct L l;
    l.p[1].y = 3;
    l.n = 1;
    printf("%d %d\n", l.p[1].y, l.n);
    printf("%d\n", l.p[0].x);
    return 0;
}
